using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;

public class TextUpdater : MonoBehaviour {

	// Use this for initialization
	void Start () {
		
	}
	
	// Update is called once per frame
	void Update () {
		GetComponent<Text>().text = string.Format("Battery {0} %", ((TelloLib.Tello.state != null) ? ("" + TelloLib.Tello.state.batteryPercentage) : " - "));
	}
}
