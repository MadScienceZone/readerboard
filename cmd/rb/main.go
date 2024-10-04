//
// rb provides a command-line tool to control readerboards and busylights.
// By default, it communicates with rbserver to request changes to the
// deployed fleet of devices. Alternatively, it can directly communicate
// with devices attached locally.
//
// Options:
//  Using rbserver
//   -to NAME[:PORT]    Host name of rbserver [default localhost:43210]
//
//  Direct connections
//   -port NAME         Serial port name (for direct connections)
//   -speed BAUDRATE    Baud rate (for direct connections)
//   -g GLOBALADDR      The global address
//   -rs485             Connection is RS-485 network instead of USB
//
//  Operations
//   -a ADDRESS         Target device address
//   -c COMMAND         Command for device
//        COMMAND          PARAMETERS
//        alloff           a
//        bitmap           a at f merge t
//        busy             a o
//        clear            a
//        color            a co
//        current          a o
//        diag-banners     a
//        flash            a l
//        font             a fo
//        graph            a v
//        light            a l
//        move             a at
//        off              a
//        query            a o
//        scroll           a lp t
//        strobe           a l
//        test             a
//        text             a al merge t tr
//        configure-device a glob newaddr rspeed uspeed
//        post             a co hold id repeat show t tr until vis
//        postlist         a id o
//        unpost           a id
//        update           set
//   -m MODEL           Hardware model
//
//  Parameters for Commands
//   -al ALIGNMENT      Align text on display
//   -at COLUMN         Matrix column to display at
//   -co COLOR          Color to use
//   -f FILE            Get text/bitmap/etc. contents from file
//   -fo IDX            Select font
//   -glob ADDR         Global device address
//   -hold DURATION     Display hold time
//   -id ID             Message ID
//   -l LLL...          List of LEDs to light
//   -lp                Loop forever
//   -merge             Merge with existing contents
//   -newaddr ADDRESS   New device address to set
//   -o FILE            Output results to file (query commands)
//   -repeat DURATION   Display repeat interval
//   -rspeed BAUD       RS-485 baud rate
//   -set K1=V1,K2=V2...Assign variables
//   -show DURATION     Display show time
//   -t TEXT            Text to be displayed
//   -tr TRANSITION     Transition effect
//   -until DATETIME    Display expiration
//   -uspeed BAUD       USB baud rate
//   -v N or RGB*8      Value to graph
//   -vis DURATION      Display visibility time
//
package main

import (
	"flag"
	"log"
	"readerboard/readerboard"
	"strings"
)

func main() {
	var serverHost = flag.String("to", "", "rbserver [host][:port]")
	var baudRate = flag.Int("speed", 9600, "Baud rate of target device")
	var portName = flag.String("port", "", "USB port name where device is attached")
	var globalAddress = flag.Int("g", 15, "Global address")
	var rs485 = flag.Bool("rs485", false, "RS-485 network instead of USB")

	var command = flag.String("c", "", "Command to send to device(s)")
	var deviceModel = flag.String("m", "rb", "Target device model (\"busylight\" / \"rb\" / \"mono\")")

	var target = flag.String("a", "", "Target device ID(s)")
	var align = flag.String("al", "", "Alignment")
	var pos = flag.Int("at", -1, "Column number to begin display")
	var color = flag.String("co", "", "Color")
	var fileName = flag.String("f", "", "File source for command")
	var fontIdx = flag.Int("fo", 0, "Font index")
	var newGlobalAddress = flag.Int("glob", -1, "New global address to set")
	var holdDuration = flag.String("hold", "", "Time to hold")
	var messageIDs = flag.String("id", "", "Message ID(s)")
	var lightCodes = flag.String("l", "", "List of lights")
	var loop = flag.Bool("lp", false, "Loop forever")
	var merge = flag.Bool("merge", false, "Merge with existing content")
	var newAddress = flag.Int("newaddr", -1, "New device address or -1 to disable")
	var outoutFile = flag.String("o", "", "Output to file")
	var repeatDuration = flag.String("repeat", "", "Repeat interval")
	var rspeed = flag.Int("rspeed", 0, "RS-485 speed to set")
	var uspeed = flag.Int("uspeed", 0, "USB speed to set")
	var value = flag.String("v", "", "Value to graph")
	var visible = flag.String("vis", "", "Visibility duration")

	flag.Parse()

	if *command == "" {
		log.Fatal("-c is required to specify the desired operation")
	}

	var err error
	var host string
	directConnection := readerboard.NetworkDescription{
		ConnectionType: USBDirect,
		Device:         *portName,
		BaudRate:       *baudRate,
	}
	if *rs485 {
		directConnection.ConnectionType = RS485Network
	}

	if *portName != "" {
		// TODO We're going to a direct connection to the device or network
		if err = directConnection.Attach("direct connection", *globalAddress); err != nil {
			log.Fatalf("Unable to open port: %v", err.Error())
		}
	} else {
		// TODO We're talking to an rbserver server
		if *serverHost == "" {
			host = "localhost:43210"
		} else if strings.HasPrefix(*serverHost, ":") {
			host = "localhost" + *serverHost
		} else {
			host = *serverHost
		}
	}

	// TODO carry out the requested operation

	switch *command {
	case "alloff":
		err = readerboard.InvokeOperation(AllLightsOff, directConnection, host,
			*target, *globalAddress, *deviceModel)
	default:
		log.Fatalf("%s is not a recognized command.", *command)
	}

	if err != nil {
		log.Fatalf("error: %v", err.Error())
	}

	/*
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
	   func main() {
	*/

}
