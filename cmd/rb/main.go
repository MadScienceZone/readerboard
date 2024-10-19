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
//   -show DURATION     Display show time
//   -t TEXT            Text to be displayed
//   -tr TRANSITION     Transition effect
//   -until DATETIME    Display expiration
//   -uspeed BAUD       USB baud rate
//   -v N or RGB*8      Value to graph
//   -vis DURATION      Display visibility time
//
//  Non-flag arguments
//   varname=value      used with update command
//
package main

import (
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"os"
	"readerboard/readerboard"
	"strconv"
	"strings"
)

func main() {
	var serverHost = flag.String("to", "", "rbserver [host][:port]")
	var baudRate = flag.Int("speed", 9600, "Baud rate of target device")
	var portName = flag.String("port", "", "USB port name where device is attached")
	var globalAddress = flag.Int("g", 15, "Global address")
	var rs485 = flag.Bool("rs485", false, "RS-485 network instead of USB")

	var command = flag.String("c", "", "Command to send to device(s)")
	//	var deviceModel = flag.String("m", "rb", "Target device model (\"busylight\" / \"rb\" / \"mono\")")

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
	var outputFile = flag.String("o", "", "Output to file")
	var repeatDuration = flag.String("repeat", "", "Repeat interval")
	var rspeed = flag.Int("rspeed", 0, "RS-485 speed to set")
	var show = flag.String("show", "", "show time")
	var text = flag.String("t", "", "text message")
	var trans = flag.String("tr", "", "transition effect")
	var uspeed = flag.Int("uspeed", 0, "USB speed to set")
	var value = flag.String("v", "", "Value to graph")
	var until = flag.String("until", "", "display until")
	var visible = flag.String("vis", "", "Visibility duration")

	flag.Parse()

	if *command == "" {
		log.Fatal("-c is required to specify the desired operation")
	}

	if *target == "" {
		log.Fatal("-a is required to specify the list of target device addresses")
	}

	var err error
	var targets []int
	for _, t := range strings.Split(*target, ",") {
		tnum, err := strconv.Atoi(t)
		if err != nil {
			log.Fatalf("-a: invalid target \"%s\": %v", t, err)
		}
		targets = append(targets, tnum)
	}

	var host string
	directConnection := readerboard.NetworkDescription{
		ConnectionType: readerboard.USBDirect,
		Device:         *portName,
		BaudRate:       *baudRate,
	}
	if *rs485 {
		directConnection.ConnectionType = readerboard.RS485Network
	}

	if *portName != "" {
		// We're going to a direct connection to the device or network
		if err = directConnection.Attach("direct connection", *globalAddress); err != nil {
			log.Fatalf("Unable to open port: %v", err.Error())
		}
	} else {
		// We're talking to an rbserver server
		if *serverHost == "" {
			host = "localhost:43210"
		} else if strings.HasPrefix(*serverHost, ":") {
			host = "localhost" + *serverHost
		} else {
			host = *serverHost
		}
	}

	// carry out the requested operation
	if *portName == "" {
		URL := "http://" + host + "/readerboard/v1/"
		// We're just passing the command on to the server, so we don't need to do all the hardware-level stuff down below.
		for _, cmd := range strings.Split(*command, ",") {
			switch cmd {
			case "alloff":
				resp, err := http.PostForm(URL+"alloff", url.Values{"a": {*target}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "bitmap":
				if *fileName == "" {
					log.Fatalf("-f option required to specify source file for image bitmap")
				}
				bitmap, err := readerboard.ImageFromASCIIFile(*fileName)
				if err != nil {
					log.Fatal(err)
				}

				var img strings.Builder
				if len(bitmap.Planes) != bitmap.Depth || (bitmap.Depth != 2 && bitmap.Depth != 4) {
					log.Fatalf("invalid bitmap (%d planes, depth %d)", len(bitmap.Planes), bitmap.Depth)
				}
				for p := 0; p < 4; p++ {
					if p > 0 {
						img.WriteString("$")
					}
					if p < len(bitmap.Planes) {
						for _, c := range bitmap.Planes[p] {
							img.WriteString(fmt.Sprintf("%02x", c))
						}
					}
				}

				resp, err := http.PostForm(URL+"bitmap", url.Values{
					"a":     {*target},
					"merge": {strconv.FormatBool(*merge)},
					"trans": {*trans},
					"pos":   {fmt.Sprintf("%d", *pos)},
					"image": {img.String()},
				})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "busy":
				resp, err := http.PostForm(URL+"busy", url.Values{"a": {*target}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				defer resp.Body.Close()
				body, err := io.ReadAll(resp.Body)
				if err != nil {
					log.Fatalf("server response error: %v", err)
				}
				if *outputFile != "" {
					f, err := os.OpenFile(*outputFile, os.O_CREATE|os.O_RDWR, 0o644)
					if err != nil {
						log.Fatalf("cannot save to \"%s\": %v", *outputFile, err)
					}
					defer func() {
						if err := f.Close(); err != nil {
							log.Fatalf("error saving \"%s\": %v", *outputFile, err)
						}
					}()
					f.Write(body)
				} else {
					fmt.Println(string(body))
				}

			case "clear":
				resp, err := http.PostForm(URL+"clear", url.Values{"a": {*target}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "color":
				resp, err := http.PostForm(URL+"color", url.Values{"a": {*target}, "color": {string(readerboard.ParseColorCode(*color))}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "current":
				resp, err := http.PostForm(URL+"current", url.Values{"a": {*target}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				defer resp.Body.Close()
				body, err := io.ReadAll(resp.Body)
				if err != nil {
					log.Fatalf("server response error: %v", err)
				}
				if *outputFile != "" {
					f, err := os.OpenFile(*outputFile, os.O_CREATE|os.O_RDWR, 0o644)
					if err != nil {
						log.Fatalf("cannot save to \"%s\": %v", *outputFile, err)
					}
					defer func() {
						if err := f.Close(); err != nil {
							log.Fatalf("error saving \"%s\": %v", *outputFile, err)
						}
					}()
					f.Write(body)
				} else {
					fmt.Println(string(body))
				}

			case "diag-banners":
				resp, err := http.PostForm(URL+"diag-banners", url.Values{"a": {*target}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "flash":
				resp, err := http.PostForm(URL+"flash", url.Values{"a": {*target}, "l": {*lightCodes}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "font":
				resp, err := http.PostForm(URL+"font", url.Values{"a": {*target}, "idx": {fmt.Sprintf("%d", *fontIdx)}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "graph":
				var barColors string
				var colors []byte
				if strings.ContainsRune(*value, ',') {
					// comma-separated list of eight colors
					colorList := strings.Split(*value, ",")
					if len(colorList) != 8 {
						log.Fatal("-value must be a single integer 0-8 or list of eight colors")
					}
					for _, color := range colorList {
						colors = append(colors, readerboard.ParseColorCode(color))
					}
					barColors = string(colors)
				} else {
					// eight color-code bytes
					if len(*value) != 8 {
						// or maybe an integer
						if quantity, err := strconv.Atoi(*value); err == nil && quantity >= 0 && quantity <= 8 {
							resp, err := http.PostForm(URL+"graph", url.Values{"a": {*target}, "v": {*value}})
							if err != nil {
								log.Fatalf("server error: %v", err)
							}
							resp.Body.Close()
							break
						} else {
							log.Fatal("-value must be a single integer 0-8 or list of eight colors")
						}
					}
					for _, color := range *value {
						colors = append(colors, readerboard.ParseColorCode(string(color)))
					}
					barColors = string(colors)
				}

				resp, err := http.PostForm(URL+"graph", url.Values{"a": {*target}, "colors": {barColors}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "light":
				resp, err := http.PostForm(URL+"light", url.Values{"a": {*target}, "l": {*lightCodes}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "move":
				resp, err := http.PostForm(URL+"move", url.Values{"a": {*target}, "pos": {fmt.Sprintf("%d", *pos)}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "off":
				resp, err := http.PostForm(URL+"off", url.Values{"a": {*target}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "query":
				resp, err := http.PostForm(URL+"query", url.Values{"a": {*target}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				defer resp.Body.Close()
				body, err := io.ReadAll(resp.Body)
				if err != nil {
					log.Fatalf("server response error: %v", err)
				}
				if *outputFile != "" {
					f, err := os.OpenFile(*outputFile, os.O_CREATE|os.O_RDWR, 0o644)
					if err != nil {
						log.Fatalf("cannot save to \"%s\": %v", *outputFile, err)
					}
					defer func() {
						if err := f.Close(); err != nil {
							log.Fatalf("error saving \"%s\": %v", *outputFile, err)
						}
					}()
					f.Write(body)
				} else {
					fmt.Println(string(body))
				}

			case "scroll":
				resp, err := http.PostForm(URL+"scroll", url.Values{"a": {*target}, "t": {*text}, "loop": {strconv.FormatBool(*loop)}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "strobe":
				resp, err := http.PostForm(URL+"strobe", url.Values{"a": {*target}, "l": {*lightCodes}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "test":
				resp, err := http.PostForm(URL+"test", url.Values{"a": {*target}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "text":
				resp, err := http.PostForm(URL+"text", url.Values{
					"a":     {*target},
					"align": {*align},
					"merge": {strconv.FormatBool(*merge)},
					"t":     {*text},
					"trans": {*trans},
				})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "configure-device":
				resp, err := http.PostForm(URL+"configure-device", url.Values{
					"a":       {*target},
					"rspeed":  {fmt.Sprintf("%d", *rspeed)},
					"uspeed":  {fmt.Sprintf("%d", *uspeed)},
					"address": {fmt.Sprintf("%d", *newAddress)},
					"global":  {fmt.Sprintf("%d", *newGlobalAddress)},
				})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "post":
				resp, err := http.PostForm(URL+"post", url.Values{
					"a":       {*target},
					"t":       {*text},
					"id":      {*messageIDs},
					"trans":   {*trans},
					"until":   {*until},
					"hold":    {*holdDuration},
					"color":   {*color},
					"visible": {*visible},
					"show":    {*show},
					"repeat":  {*repeatDuration},
				})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "postlist":
				resp, err := http.PostForm(URL+"postlist", url.Values{"id": {*messageIDs}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				defer resp.Body.Close()
				body, err := io.ReadAll(resp.Body)
				if err != nil {
					log.Fatalf("server response error: %v", err)
				}
				if *outputFile != "" {
					f, err := os.OpenFile(*outputFile, os.O_CREATE|os.O_RDWR, 0o644)
					if err != nil {
						log.Fatalf("cannot save to \"%s\": %v", *outputFile, err)
					}
					defer func() {
						if err := f.Close(); err != nil {
							log.Fatalf("error saving \"%s\": %v", *outputFile, err)
						}
					}()
					f.Write(body)
				} else {
					fmt.Println(string(body))
				}

			case "unpost":
				resp, err := http.PostForm(URL+"unpost", url.Values{"id": {*messageIDs}})
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()

			case "update":
				updateList := make(url.Values)
				for _, setting := range flag.Args() {
					if delim := strings.IndexRune(setting, '='); delim > 0 {
						updateList[setting[0:delim]] = []string{setting[delim+1:]}
					} else {
						log.Fatalf("variable setting \"%s\" must be in the form <variable>=<value>", setting)
					}
				}
				resp, err := http.PostForm(URL+"update", updateList)
				if err != nil {
					log.Fatalf("server error: %v", err)
				}
				resp.Body.Close()
			default:
				log.Fatalf("%s: not a valid command", cmd)
			}
		}
	}

	/*
		switch *command {
		case "alloff":
			err = readerboard.InvokeOperation(readerboard.AllLightsOff, directConnection, host, *target, *globalAddress, *deviceModel)
		default:
			log.Fatalf("%s is not a recognized command.", *command)
		}

		if err != nil {
			log.Fatalf("error: %v", err.Error())
		}
	*/
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
