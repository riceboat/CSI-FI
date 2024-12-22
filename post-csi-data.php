<?php

/*
  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/esp32-esp8266-mysql-database-php/
  
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files.
  
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
*/
ini_set("memory_limit", "1024M");
$servername = "localhost";

// REPLACE with your Database name
$dbname = "csi_data";
// REPLACE with Database user
$username = "admin";
// REPLACE with Database user password
$password = "raspberry";

// Keep this API Key value to be compatible with the ESP32 code provided in the project page. 
// If you change this value, the ESP32 sketch needs to match
$api_key_value = "1234";

if ($_SERVER["REQUEST_METHOD"] == "POST") {
    $api_key = test_input($_POST["api-key"]);
    if($api_key == $api_key_value) {
        $rssi = test_input($_POST["rssi"]);
		$rate = test_input($_POST["rate"]);
		$sig_mode = test_input($_POST["sig_mode"]);
		$mcs  = test_input($_POST["mcs"]);
		$cwb = test_input($_POST["cwb"]);
		$smoothing = test_input($_POST["smoothing"]);
		$not_sounding = test_input($_POST["not_sounding"]);
		$aggregation = test_input($_POST["aggregation"]);
		$stbc = test_input($_POST["stbc"]);
		$fec_coding = test_input($_POST["fec_coding"]);
		$sgi = test_input($_POST["sgi"]);
		$noise_floor = test_input($_POST["noise_floor"]);
		$ampdu_cnt = test_input($_POST["ampdu_cnt"]);
		$channel = test_input($_POST["channel"]);
		$secondary_channel = test_input($_POST["secondary_channel"]);
		$timestamp = test_input($_POST["timestamp"]);
		$ant = test_input($_POST["ant"]);
		$sig_len = test_input($_POST["sig_len"]);
		$rx_state = test_input($_POST["rx_state"]);
		$esp_id = test_input($_POST["esp_id"]);
		$buffer_len = test_input($_POST["buffer_len"]);
		$buffer = test_input($_POST["buffer"]);
		$reading_time = floor(microtime(true) * 1000);
        // Create connection
        $conn = new mysqli($servername, $username, $password, $dbname);
        // Check connection
        if ($conn->connect_error) {
            die("Connection failed: " . $conn->connect_error);
        }
        $sql = "INSERT INTO CSIData (rssi,rate,sig_mode,mcs,cwb,smoothing,not_sounding,aggregation,stbc,fec_coding,sgi,noise_floor,ampdu_cnt,channel,secondary_channel,timestamp,ant,sig_len,rx_state,esp_id,reading_time, buffer_len, buffer)
        VALUES ('" . $rssi . "','" . $rate . "','" . $sig_mode . "','" . $mcs . "','" . $cwb . "','" . $smoothing . "','" . $not_sounding . "','" . $aggregation . "','" . $stbc . "','" . $fec_coding . "','" . $sgi . "','" . $noise_floor . "','" . $ampdu_cnt . "','" . $channel . "','" . $secondary_channel . "','" . $timestamp . "','" . $ant . "','" . $sig_len . "','" . $rx_state . "','" . $esp_id . "','" . $reading_time . "','" . $buffer_len . "', '" . $buffer . "')";
        if ($conn->query($sql) === TRUE) {
            echo "New record created successfully";
        } 
        else {
            echo "Error: " . $sql . "<br>" . $conn->error;
        }
        $conn->close();
    }
    else {
        echo "Wrong API Key provided.";
    }

}
else {
    echo "No data posted with HTTP POST!";
}

function test_input($data) {
    $data = trim($data);
    $data = stripslashes($data);
    $data = htmlspecialchars($data);
    return $data;
}
