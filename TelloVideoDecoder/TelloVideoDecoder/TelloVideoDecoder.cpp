// FFmpeg based UDP video stream decoder

#include "PlatformBase.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
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

using namespace std;

namespace {
	void debug_log(const char* msg)
	{
#if defined(_WIN32) && defined(_DEBUG)
		OutputDebugStringA(msg);
		OutputDebugStringA("\r\n");
#endif
		cout << msg << endl;
	}


	const size_t PIXELS = 1280 * 720;
	const size_t BPP = 4;
	const size_t IMAGE_SIZE_IN_BYTE = PIXELS * BPP;

	class StaticObject
	{
	public:
		StaticObject()
		{
#ifdef UNITY_WIN
			WSADATA data;
			WSAStartup(MAKEWORD(2, 0), &data);
#endif
		}
		~StaticObject()
		{
#ifdef UNITY_WIN
			WSACleanup();
#endif
		}
	};

	StaticObject _staticObject;
}

class MyUdpClient
{
public:
	MyUdpClient(bool* isRunning)
		:
#ifdef UNITY_WIN
		sock(INVALID_SOCKET)
#else
		sock(-1)
#endif
		, isRunning(isRunning)
	{
	}

	~MyUdpClient()
	{
		close();
	}

	bool open(int port)
	{
		sock = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef UNITY_WIN
		if (sock != INVALID_SOCKET) {
#else
		if (sock != -1) {
#endif
			sockaddr_in addr;
			memset(&addr, 0, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_port = htons(port);
			addr.sin_addr.s_addr = htonl(INADDR_ANY);

			int status = ::bind(sock, (sockaddr*)&addr, sizeof(addr));
#ifdef UNITY_WIN
			if (status != SOCKET_ERROR) {
				u_long val = 1;
				ioctlsocket(sock, FIONBIO, &val);
				return true;
			}
			else {
				closesocket(sock);
				sock = INVALID_SOCKET;
			}
#else
			if (status != -1) {
				int val = 1;
				ioctl(sock, FIONBIO, &val);
				return true;
			}
			else {
                ::close(sock);
				sock = -1;
			}
#endif
		}
		return false;
	}

	void close()
	{
#ifdef UNITY_WIN
		if (sock != INVALID_SOCKET) {
			closesocket(sock);
			sock = INVALID_SOCKET;
		}
#else
		if (sock != -1) {
            ::close(sock);
			sock = -1;
		}
#endif
	}

	int read(char* buffer, int buffer_size)
	{
#ifdef UNITY_WIN
		if (sock == INVALID_SOCKET)
#else
		if (sock == -1)
#endif
			return -1;

		int read_size = -1;
		while (*isRunning) {

            read_size = static_cast<int>(recv(sock, buffer, static_cast<size_t>(buffer_size), 0));
            if (read_size < 1) {
#ifdef UNITY_WIN
                if (WSAGetLastError() == WSAEWOULDBLOCK) {
                    this_thread::yield();
#else
                if (errno == EAGAIN) {
                    this_thread::yield();
//                    this_thread::sleep_for(std::chrono::milliseconds(5));
#endif
                    continue;
                }
                else {
                    break;
                }
            } else {
                break;
            }
		}
            
		return read_size;
	}
private:
#ifdef UNITY_WIN
	SOCKET sock;
#else
	int sock;
#endif
	bool* isRunning;
};

class MyTelloUdpAVIOContext
{
public:
	MyTelloUdpAVIOContext(const char* address, int port, bool* isRunning)
		: avio_ctx(nullptr), buffer2(nullptr), udp(isRunning) {
		//avformat_network_init(); // not necessary

		buffer = static_cast<unsigned char*>(av_malloc(BUFFER_SIZE));
		avio_ctx = avio_alloc_context(buffer, BUFFER_SIZE, 0, this, &MyTelloUdpAVIOContext::read, nullptr, nullptr);

		if (!udp.open(port)) {
			debug_log("UDP open failed");
		}
	}

	~MyTelloUdpAVIOContext() {
		av_freep(&avio_ctx->buffer);
		av_freep(&avio_ctx);
		if (buffer2 != nullptr)
			delete[] buffer2;

		udp.close();
	}

	static int read(void *opaque, unsigned char *buf, int buf_size) {
		MyTelloUdpAVIOContext* h = static_cast<MyTelloUdpAVIOContext*>(opaque);

		stringstream ss;

		if (buf_size + 2 > h->buffer2_size) {
			if (h->buffer2 != nullptr)
				delete[]  h->buffer2;
			h->buffer2_size = buf_size + 2;
			h->buffer2 = new char[h->buffer2_size];
		}
		int read_size = h->udp.read(h->buffer2, h->buffer2_size);
		if (read_size <= 0) {
			return 0;
		}

		uint8_t b0 = (uint8_t)h->buffer2[0];
		uint8_t b1 = (uint8_t)h->buffer2[1];
		ss << "buf_size: " << buf_size << ", read_size:" << read_size << " [0]:" << (int)b0 << " [1]:" << (int)b1;
		debug_log(ss.str().c_str());

		read_size -= 2;
		memcpy(buf, h->buffer2 + 2, read_size);

		return read_size;
	}

	AVIOContext* get() { return avio_ctx; }

private:
	static const int BUFFER_SIZE = 32768;
	unsigned char* buffer;
	AVIOContext * avio_ctx;
	MyUdpClient udp;

	char* buffer2;
	int buffer2_size;
};

class MyVideoDecoder {
public:
	MyVideoDecoder() :
		avioContext(nullptr),
		fmt_ctx(nullptr),
		video_stream(nullptr),
		codec(nullptr),
		codec_context(nullptr),
		frame(nullptr) {

	}
	~MyVideoDecoder() {
	}

public:
	bool run(uint8_t* destination, size_t size, mutex* mtx, bool* isRunning) {

		if (!open(isRunning))
			return false;

		uint8_t* rgbaLineBuffer = nullptr;
		int rgbaLineBufferSize = 0;

		// Read frame
		while ((*isRunning) && av_read_frame(fmt_ctx, &packet) == 0) {
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
						if (rgbaLineBuffer == nullptr)
							rgbaLineBuffer = new uint8_t[linesz];
						else if (rgbaLineBufferSize < linesz) {
							rgbaLineBufferSize = linesz;
							delete[] rgbaLineBuffer;
							rgbaLineBuffer = new uint8_t[linesz];
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
					mtx->lock();
					memcpy(destination, frame_rgba->data[0], size);
					mtx->unlock();
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

private:
	bool open(bool* isRunning) {

		avioContext = new MyTelloUdpAVIOContext(TELLO_ADDRESS.c_str(), TELLO_VIDEO_PORT, isRunning);
		fmt_ctx = avformat_alloc_context();
		fmt_ctx->pb = avioContext->get();

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

		av_dump_format(fmt_ctx, 0, TELLO_ADDRESS.c_str(), 0);

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

		if (avioContext != nullptr) {
			delete avioContext;
			avioContext = nullptr;
		}
	}
private:
	static const string TELLO_ADDRESS;
	static const int TELLO_VIDEO_PORT;

	MyTelloUdpAVIOContext* avioContext;
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

const string MyVideoDecoder::TELLO_ADDRESS("192.168.0.1");
const int MyVideoDecoder::TELLO_VIDEO_PORT(6038);

extern "C" {

	struct TelloVideoDecoderContext {
		bool isRunning_ = true;
		uint8_t* imageBuffer_ = nullptr;
		thread* thread_ = nullptr;
		mutex mutex_;
	};

	void decode(TelloVideoDecoderContext* ctx)
	{
		MyVideoDecoder decoder;
		while (ctx->isRunning_) {
			decoder.run(ctx->imageBuffer_, IMAGE_SIZE_IN_BYTE, &ctx->mutex_, &ctx->isRunning_);
			this_thread::yield();
		}
	}

	TelloVideoDecoderContext* TelloVideoDecoder_Open()
	{
		TelloVideoDecoderContext* ctx = new TelloVideoDecoderContext();

		ctx->imageBuffer_ = new uint8_t[IMAGE_SIZE_IN_BYTE];
		for (int i = 0; i < IMAGE_SIZE_IN_BYTE; i += BPP) {
			for (int j = 0; j < BPP; j++) {
				if (j == 3)
					ctx->imageBuffer_[i + j] = 0xff;
				else if (j == 2)
					ctx->imageBuffer_[i + j] = 0x10;
				else
					ctx->imageBuffer_[i + j] = 0;
			}
		}

		ctx->isRunning_ = true;

		ctx->thread_ = new thread(decode, ctx);

		return ctx;
	}

	void TelloVideoDecoder_Close(TelloVideoDecoderContext* ctx)
	{
		ctx->isRunning_ = false;

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

	void TelloVideoDecoder_ModifyTexturePixels(TelloVideoDecoderContext* ctx, void* data, int width, int height)
	{
		//debug_log("TelloVideoDecoder_ModifyTexturePixels begin");

		int size = width * height * 4;
		size = min<int>(size, IMAGE_SIZE_IN_BYTE);

		if (ctx != nullptr) {
			ctx->mutex_.lock();
			if (ctx->imageBuffer_ != nullptr)
				memcpy(data, ctx->imageBuffer_, size);
			ctx->mutex_.unlock();
		}
		//debug_log("TelloVideoDecoder_ModifyTexturePixels end");
	}
}
