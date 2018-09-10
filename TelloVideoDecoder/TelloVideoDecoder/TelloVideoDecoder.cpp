// FFmpeg based UDP video stream decoder

#include "PlatformBase.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <sstream>
#include <thread>

extern "C" {
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#ifdef UNITY_WIN
#include <winsock2.h>
#include <ws2tcpip.h>

#define _CRTDBG_MAP_ALLOC #include <stdlib.h> #include <crtdbg.h>
#define new ::new(_NORMAL_BLOCK, __FILE__, __LINE__)
#define malloc(X) _malloc_dbg(X,_NORMAL_BLOCK,__FILE__,__LINE__)
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "Unity/IUnityInterface.h"
#include "RenderAPI.h"
#include "PlatformBase.h"

#if defined(UNITY_ANDROID)
#include <android/log.h>
#endif

using namespace std;

namespace {
	void debug_log(const char* msg)
	{
#if defined(UNITY_WIN)
	#if defined(_DEBUG)
		OutputDebugStringA(msg);
		OutputDebugStringA("\r\n");
	#endif
#elif defined (UNITY_ANDROID)
	// #if !defined(NDEBUG)
		__android_log_print(ANDROID_LOG_VERBOSE, "TelloVideoDecoder.cpp", "%s\n", msg);
	// #endif
#else
	#if !defined(NDEBUG)
		cout << msg << endl;
	#endif
#endif
	}

	const size_t PIXELS = 1280 * 720;
	const size_t BPP = 4;
	const size_t IMAGE_SIZE_IN_BYTES = PIXELS * BPP;

#ifdef UNITY_WIN
	class StaticObjectForCleanup
	{
	public:
		StaticObjectForCleanup()
		{
			WSADATA data;
			WSAStartup(MAKEWORD(2, 0), &data);
		}
		~StaticObjectForCleanup()
		{
			WSACleanup();
		}
	};
	StaticObjectForCleanup _staticObject;
#endif
}

class MyVideoDecoder {
public:
	MyVideoDecoder() :
		mtx(nullptr),
		cond(nullptr),
		isRunning(nullptr),
		avio_ctx(nullptr),
		fmt_ctx(nullptr),
		video_stream(nullptr),
		codec(nullptr),
		codec_context(nullptr),
		frame(nullptr) {
	}
	~MyVideoDecoder() {
	}

public:
	bool run(uint8_t* destination, size_t size, mutex* mtx, condition_variable* cond, bool* isRunning) {
		if (!open(mtx, cond, isRunning))
			return false;

		uint8_t* rgbaLineBuffer = nullptr;
		int rgbaLineBufferSize = 0;

		// Read frame
		while (true) {
			{
				lock_guard<mutex> lock(*mtx);
				if ((!*isRunning))
					break;
			}
			if (av_read_frame(fmt_ctx, &packet) < 0) {
				this_thread::yield();
				continue;
			}

			if (packet.stream_index == video_stream->index) {
				if (avcodec_send_packet(codec_context, &packet) != 0) {
					debug_log("avcodec_send_packet failed\n");
				}
				while ((*isRunning) && avcodec_receive_frame(codec_context, frame) == 0) {
					sws_scale(convert_context, (const uint8_t* const*)frame->data, frame->linesize, 0, codec_context->height, frame_rgba->data, frame_rgba->linesize);
					stringstream ss;
					ss << "Decoded frame: " << (unsigned long)frameCounter << " width: " << codec_context->width << " height: " << codec_context->height;
#if 1
					if (codec_context->height > 0) {

						// Flip
						int linesz = frame_rgba->linesize[0];
						if (rgbaLineBuffer == nullptr) {
							rgbaLineBufferSize = linesz;
							rgbaLineBuffer = new uint8_t[rgbaLineBufferSize];
						}
						else if (rgbaLineBufferSize < linesz) {
							delete[] rgbaLineBuffer;
							rgbaLineBufferSize = linesz;
							rgbaLineBuffer = new uint8_t[rgbaLineBufferSize];
						}

						uint8_t* plineU = frame_rgba->data[0];
						uint8_t* plineL = plineU + linesz * (codec_context->height - 1);
						int hh = codec_context->height / 2;
						for (int i = 0; i < hh; i++) {
							memcpy(rgbaLineBuffer, plineU, linesz);
							memcpy(plineU, plineL, linesz);
							memcpy(plineL, rgbaLineBuffer, linesz);
							plineU += linesz;
							plineL -= linesz;
						}
					}
#endif

					debug_log(ss.str().c_str());

					{
						lock_guard<mutex> lock(*mtx);
						memcpy(destination, frame_rgba->data[0], size);
					}

					frameCounter++;
				}
			}
			av_packet_unref(&packet);
			this_thread::yield();
		}

		if (rgbaLineBuffer != nullptr)
			delete[] rgbaLineBuffer;

		close();
		return true;
	}

	void putVideoData(uint8_t* data, int size) {
		if (fragmentQueue.size() < MAX_FRAGMENT_COUNT) {
			fragmentQueue.push(new Fragment(data, size));
		}
	}

	static int read(void *opaque, unsigned char *buf, int buf_size) {
		MyVideoDecoder* h = static_cast<MyVideoDecoder*>(opaque);

		chrono::steady_clock::time_point start = chrono::steady_clock::now();
		while (true) {
			{
				lock_guard<mutex> lock(*h->mtx);
				if ((!*h->isRunning))
					break;
			}

			if (!h->fragmentQueue.empty()) {
				lock_guard<mutex> lock(*h->mtx);
				Fragment* f = h->fragmentQueue.front();
				int size = min<int>(f->size, buf_size);
				if (size > 2) {

					stringstream ss;
					ss << "VideoData data[0]: " << (int)f->buffer[0] << " data[1]: " << (int)f->buffer[1];
					debug_log(ss.str().c_str());

					size -= 2;
					memcpy(buf, f->buffer + 2, size);
				}
				h->fragmentQueue.pop();
				delete f;
				return size;
			}
			else {
				unique_lock<mutex> lock(*h->mtx);
				cv_status result = h->cond->wait_for(lock, chrono::seconds(1));
				if (result == std::cv_status::timeout) {
					debug_log("timeout");
					break;
				}
			}
		}
		return 0;
	}

private:
	bool open(mutex* mtx, condition_variable* cond, bool* isRunning) {
		this->mtx = mtx;
		this->cond = cond;
		this->isRunning = isRunning;

		unsigned char* avio_buffer = static_cast<unsigned char*>(av_malloc(BUFFER_SIZE));
		avio_ctx = avio_alloc_context(avio_buffer, BUFFER_SIZE, 0, this, &MyVideoDecoder::read, nullptr, nullptr);

		fmt_ctx = avformat_alloc_context();
		fmt_ctx->pb = avio_ctx;

		/// using ctx
		int ret;
		ret = avformat_open_input(&fmt_ctx, nullptr, nullptr, nullptr);
		if (ret < 0) {
			debug_log("Could not open input\n");
			close();
			return false;
		}

		ret = avformat_find_stream_info(fmt_ctx, nullptr);
		if (ret < 0) {
			debug_log("Could not find stream information\n");
			close();
			return false;
		}

		// decode
		for (int i = 0; i < (int)fmt_ctx->nb_streams; ++i) {
			if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
				video_stream = fmt_ctx->streams[i];
				break;
			}
		}
		if (video_stream == nullptr) {
			debug_log("No video stream ...\n");
			close();
			return false;
		}
		if (video_stream->codecpar == nullptr) {
			debug_log("No codec parameter ...\n");
			close();
			return false;
		}

		codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
		if (codec == nullptr) {
			debug_log("No supported decoder ...\n");
			close();
			return false;
		}

		codec_context = avcodec_alloc_context3(codec);
		if (codec_context == nullptr) {
			debug_log("avcodec_alloc_context3 failed\n");
			close();
			return false;
		}

		if (avcodec_parameters_to_context(codec_context, video_stream->codecpar) < 0) {
			debug_log("avcodec_parameters_to_context failed\n");
			close();
			return false;
		}

		if (avcodec_open2(codec_context, codec, nullptr) != 0) {
			debug_log("avcodec_open2 failed\n");
			close();
			return false;
		}

		frame = av_frame_alloc();

		// Color converter
		frame_rgba = av_frame_alloc();
		buffer = (uint8_t*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_RGBA, codec_context->width, codec_context->height, 1));
		av_image_fill_arrays(frame_rgba->data, frame_rgba->linesize, buffer, AV_PIX_FMT_RGBA, codec_context->width, codec_context->height, 1);
		//av_image_fill_arrays(frame->data, frame->linesize, buffer, AV_PIX_FMT_RGBA, codec_context->width, codec_context->height, 1);
		convert_context = sws_getContext(codec_context->width, codec_context->height, codec_context->pix_fmt, codec_context->width, codec_context->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
		return true;
	}

	void close() {
		// flush decoder
		if (codec_context != nullptr && avcodec_send_packet(codec_context, nullptr) != 0) {
			debug_log("avcodec_send_packet failed");
		}

		if (codec_context != nullptr && frame != nullptr && convert_context != nullptr && frame_rgba != nullptr) {
			while (avcodec_receive_frame(codec_context, frame) == 0) {
				sws_scale(convert_context, (const uint8_t* const*)frame->data, frame->linesize, 0, codec_context->height, frame_rgba->data, frame_rgba->linesize);
				//debug_log("%u width: %d, height: %d\n", (unsigned long)frameCounter, codec_context->width, codec_context->height);
				frameCounter++;
			}
		}

		if (convert_context != nullptr) {
			sws_freeContext(convert_context);
			convert_context = nullptr;
		}

		if (buffer != nullptr) {
			av_free(buffer);
			buffer = nullptr;
		}

		if (frame_rgba != nullptr) {
			av_frame_free(&frame_rgba);
			frame_rgba = nullptr;
		}

		if (frame != nullptr) {
			av_frame_free(&frame);
			frame = nullptr;
		}

		if (frame_rgba != nullptr) {
			av_frame_free(&frame_rgba);
			frame_rgba = nullptr;
		}
		if (codec_context != nullptr) {
			avcodec_free_context(&codec_context);
			codec_context = nullptr;
		}

		if (fmt_ctx != nullptr) {
			avformat_close_input(&fmt_ctx);
		}

		if (fmt_ctx != nullptr) {
			avformat_free_context(fmt_ctx);
			fmt_ctx = nullptr;
		}

		if (avio_ctx != nullptr) {
			av_freep(&avio_ctx->buffer);
			av_freep(&avio_ctx);
			avio_ctx = nullptr;
		}

		{
			lock_guard<mutex> lock(*mtx);
			while (!fragmentQueue.empty()) {
				Fragment* f = fragmentQueue.front();
				delete f;
				fragmentQueue.pop();
			}
		}
		mtx = nullptr;
		cond = nullptr;
		isRunning = nullptr;
	}
private:
	static const int BUFFER_SIZE = IMAGE_SIZE_IN_BYTES;
	static const int MAX_FRAGMENT_COUNT = 16;

	mutex* mtx;
	condition_variable* cond;
	bool* isRunning;

	AVIOContext* avio_ctx;
	class Fragment {
	public:
		uint8_t* buffer;
		int size;
		Fragment(uint8_t* buf, int sz) {
			size = sz;
			buffer = new uint8_t[size];
			memcpy(buffer, buf, size);
		}
		~Fragment() {
			delete[] buffer;
		}
	private:
		Fragment() = delete;
		Fragment(const Fragment&) = delete;
		Fragment& operator=(const Fragment&) = delete;
	};
	queue<Fragment*> fragmentQueue;

	AVFormatContext* fmt_ctx;
	AVStream* video_stream;
	AVCodec* codec;
	AVCodecContext* codec_context = nullptr;
	AVFrame* frame = nullptr;
	AVPacket packet = AVPacket();

	AVFrame* frame_rgba = nullptr;
	uint8_t* buffer = nullptr;
	SwsContext* convert_context = nullptr;

	uint64_t frameCounter = 0;

};

extern "C" {

	struct TelloVideoDecoderContext {
		bool isRunning_ = true;
		uint8_t* imageBuffer_ = nullptr;
		size_t imageBufferSize = 0;
		thread* thread_ = nullptr;
		mutex mutex_;
		condition_variable condition_;
		MyVideoDecoder* decoder;
	};

	void Decode(TelloVideoDecoderContext* ctx)
	{
		MyVideoDecoder decoder;
		{
			lock_guard<mutex> lock(ctx->mutex_);
			ctx->decoder = &decoder;
		}
		while (ctx->isRunning_) {
			decoder.run(ctx->imageBuffer_, ctx->imageBufferSize, &ctx->mutex_, &ctx->condition_, &ctx->isRunning_);
			//this_thread::yield();
			this_thread::sleep_for(chrono::seconds(1));
		}
		{
			lock_guard<mutex> lock(ctx->mutex_);
			ctx->decoder = nullptr;
		}
	}

	TelloVideoDecoderContext* TelloVideoDecoder_Open()
	{
		TelloVideoDecoderContext* ctx = new TelloVideoDecoderContext();

		ctx->imageBufferSize = IMAGE_SIZE_IN_BYTES;
		ctx->imageBuffer_ = new uint8_t[ctx->imageBufferSize];
		memset(ctx->imageBuffer_, 0x80, ctx->imageBufferSize);

		ctx->isRunning_ = true;
		ctx->thread_ = new thread(Decode, ctx);

		return ctx;
	}

	void TelloVideoDecoder_Close(TelloVideoDecoderContext* ctx)
	{
		ctx->isRunning_ = false;
		ctx->condition_.notify_all();

		if (ctx->thread_ != nullptr) {
			if (ctx->thread_->joinable()) {
				ctx->thread_->join();
				delete ctx->thread_;
				ctx->thread_ = nullptr;
			}
		}

		if (ctx->imageBuffer_ != nullptr) {
			delete[] ctx->imageBuffer_;
			ctx->imageBuffer_ = nullptr;
		}

		delete ctx;
		ctx = nullptr;
	}

	void TelloVideoDecoder_ModifyTexturePixels(TelloVideoDecoderContext* ctx, void* data, int width, int height, int rowPitch)
	{
		//debug_log("TelloVideoDecoder_ModifyTexturePixels begin");

		size_t size = static_cast<size_t>(rowPitch * height);
		size = min<size_t>(size, ctx->imageBufferSize);

		if (ctx != nullptr) {
			lock_guard<mutex> lock(ctx->mutex_);
			if (ctx->imageBuffer_ != nullptr)
				memcpy(data, ctx->imageBuffer_, size);
		}
		//debug_log("TelloVideoDecoder_ModifyTexturePixels end");
	}

	void TelloVideoDecoder_PutVideoData(TelloVideoDecoderContext* ctx, void* data, int size) {
		if (ctx != nullptr) {
			lock_guard<mutex> lock(ctx->mutex_);
			if (size > 2 && ctx->decoder != nullptr) {
				uint8_t* p = static_cast<uint8_t*>(data);
				ctx->decoder->putVideoData(p, size);
				ctx->condition_.notify_all();
			}
		}
	}
}
