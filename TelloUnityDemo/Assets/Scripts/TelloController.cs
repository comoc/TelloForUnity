using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using TelloLib;
using System.Net.Sockets;
using System.Net;
using System.Threading;
using System;

public class TelloController : SingletonMonoBehaviour<TelloController> {

	private static bool isLoaded = false;

	private byte[] encodedDataPool;
	private int encodedDataFilled = 0;

	override protected void Awake()
	{
		if (!isLoaded) {
			DontDestroyOnLoad(this.gameObject);
			isLoaded = true;
		}
		base.Awake();

		Tello.onConnection += Tello_onConnection;
		Tello.onUpdate += Tello_onUpdate;
		Tello.onVideoData += Tello_onVideoData;


		encodedDataPool = new byte[1280 * 720 * 3];

	}

	private void Start()
	{
		Tello.start();
	}

	void OnApplicationQuit()
	{
		Tello.stop();
	}

	// Update is called once per frame
	void Update () {

		if (Input.GetKeyDown(KeyCode.T)) {
			Tello.takeOff();
		} else if (Input.GetKeyDown(KeyCode.L)) {
			Tello.land();
		}

		float lx = 0f;
		float ly = 0f;
		float rx = 0f;
		float ry = 0f;

		if (Input.GetKey(KeyCode.UpArrow)) {
			ry = 1;
		}
		if (Input.GetKey(KeyCode.DownArrow)) {
			ry = -1;
		}
		if (Input.GetKey(KeyCode.RightArrow)) {
			rx = 1;
		}
		if (Input.GetKey(KeyCode.LeftArrow)) {
			rx = -1;
		}
		if (Input.GetKey(KeyCode.W)) {
			ly = 1;
		}
		if (Input.GetKey(KeyCode.S)) {
			ly = -1;
		}
		if (Input.GetKey(KeyCode.D)) {
			lx = 1;
		}
		if (Input.GetKey(KeyCode.A)) {
			lx = -1;
		}
		Tello.controllerState.setAxis(lx, ly, rx, ry);

	}

	private void Tello_onUpdate(Tello.FlyData state)
	{
		//throw new System.NotImplementedException();
		Debug.Log("Tello_onUpdate : " + state);
	}

	private void Tello_onConnection(Tello.ConnectionState newState)
	{
		//throw new System.NotImplementedException();
		//Debug.Log("Tello_onConnection : " + newState);
		if (newState == Tello.ConnectionState.Connected) {
			Tello.setPicVidMode(1); // 0: picture, 1: video
			Tello.setVideoBitRate((int)Tello.VideoBitRate.VideoBitRateAuto);
			//Tello.setEV(0);
			Tello.requestIframe();
		}

	}

	private void Tello_onVideoData(int fragmentIndex, byte[] data)
	{
		if (fragmentIndex == 0)
			encodedDataFilled = 0;
		Debug.Log("Tello_onVideoData: " + fragmentIndex);
		int length = data.Length - 2;
		Buffer.BlockCopy(data, 2, encodedDataPool, encodedDataFilled, length);
		encodedDataFilled += length;
	}

}
