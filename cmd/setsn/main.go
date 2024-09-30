//
// setsn changes the serial number of the Readerboard or Busylight device
// attached via USB to the computer. This will be persistent for that
// device if it has an EEPROM storage capability.
//
// Options:
//    -model type    Target device type:
//                      rb        Readerboard 3.x RGB (default)
//                      mono      Readerboard 3.x Monochrome
//                      busylight Busylight 2.x
//    -port name     Name of port (e.g., "/dev/ttyACM0") where device is attached
//    -sn serialno   Desired serial number to set on device
//    -speed rate    Baud rate for connection (default 9600)
//
package main

import (
	"busylight/readerboard"
	"encoding/json"
	"flag"
	"log"
	"strings"
)

func main() {
	var baudRate = flag.Int("speed", 9600, "Baud rate of target device")
	var portName = flag.String("port", "", "USB port name where device is attached")
	var serialNumber = flag.String("sn", "", "Serial number to set")
	var deviceModel = flag.String("model", "rb", "Target device model (\"busylight\" / \"rb\" / \"mono\")")
	flag.Parse()

	if *portName == "" || *serialNumber == "" {
		log.Fatal("-port and -sn options are required")
	}

	if len(*serialNumber) > 6 {
		log.Fatal("serial numbers must be 6 characters or fewer")
	}

	if !strings.HasPrefix(*serialNumber, "B") && !strings.HasPrefix(*serialNumber, "RB") {
		log.Print("suggestion: serial numbers should begin with B for busylight and RB for readerboard units (suggestion only)")
	}

	if strings.ContainsAny(*serialNumber, "$\033") {
		log.Fatal("serial number must not contain '$' or ESC characters")
	}

	var port readerboard.DirectDriver
	var dtype readerboard.HardwareModel
	if err := json.Unmarshal([]byte("\""+*deviceModel+"\""), &dtype); err != nil {
		log.Fatalf("invalid value for -model (%v)", err)
	}
	if dtype != readerboard.Busylight2 && dtype != readerboard.Readerboard3RGB && dtype != readerboard.Readerboard3Mono {
		log.Fatal("device type is not supported for this operation")
	}

	if err := port.Attach("direct device", *portName, *baudRate, 0); err != nil {
		log.Fatal(err)
	}
	defer port.Detach()

	log.Printf("writing serial number \"%s\" to \"%s\"...", *serialNumber, *portName)
	if err := port.SendBytes([]byte("\004=#" + *serialNumber + "$#=\004")); err != nil {
		log.Fatal(err)
	}

	log.Printf("verifying...")
	if err := port.SendBytes([]byte{'Q', 4}); err != nil {
		log.Fatal(err)
	}
	receivedData, err := port.Receive()
	if err != nil {
		log.Fatal(err)
	}

	_, parser := readerboard.Query()
	deviceStatus, err := parser(dtype, receivedData)
	if err != nil {
		log.Fatal(err)
	}
	if deviceStatus.(readerboard.DeviceStatus).Serial != *serialNumber {
		log.Fatalf("FAILED: we programmed serial number \"%s\" but the device reported serial number \"%s\"!", *serialNumber, deviceStatus.(readerboard.DeviceStatus).Serial)
	}
	log.Printf("device confirmed serial number \"%s\"", deviceStatus.(readerboard.DeviceStatus).Serial)
}
