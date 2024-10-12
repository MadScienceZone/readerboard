//
// setsn changes the serial number of the Readerboard or Busylight device
// attached via USB to the computer. This will be persistent for that
// device if it has an EEPROM storage capability.
//
// It can also be used to set other persistent configuration values on the unit.
//
// Options:
//   Operational Parameters
//     -port name          Name of port (e.g., "/dev/ttyACM0") where device is attached
//     -speed rate         Baud rate for connection (default 9600)
//     -force              Assume "yes" to confirmation prompt
//   Settings
//     -reset              Reset configuration to factory defaults
//     -sn serialno        Desired serial number to set on device
//     -addr address       Set RS-485 device address (0-63)
//     -global address     Set RS-485 global address (0-15)
//     -usb rate           Set device's buad rate for USB port
//     -rs485 rate         Set device's buad rate for RS-485 port
//
package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"readerboard/readerboard"
	"strings"
	"time"

	"github.com/MadScienceZone/go-gma/v5/util"
)

func main() {
	var baudRate = flag.Int("speed", 9600, "Baud rate of target device")
	var portName = flag.String("port", "", "USB port name where device is attached")
	var force = flag.Bool("force", false, "Proceed anyway without prompting")

	var serialNumber = flag.String("sn", "", "Serial number to set")
	var newAddress = flag.Int("addr", -1, "Set new RS-485 device address or -1 to disable RS-485")
	var newGlobal = flag.Int("global", 15, "Set new global device ID")
	var newUSBbaud = flag.Int("usb", 9600, "Set device's baud rate for USB interface")
	var new485baud = flag.Int("rs485", 9600, "Set device's baud rate for RS-485 interface")
	var reset = flag.Bool("reset", false, "Set addr=OFF, global=15, usb=9600, rs485=9600")
	flag.Parse()

	if *portName == "" {
		log.Fatal("-port option is required")
	}

	if *serialNumber == "" && *newAddress == -1 && *newGlobal == 15 && *newUSBbaud == 9600 && *new485baud == 9600 && !*reset {
		log.Fatal("-sn and/or at least one of -reset, -addr, -global, -usb, or -rs485 are required.")
	}

	if *serialNumber != "" {
		if len(*serialNumber) > 6 {
			log.Fatal("serial numbers must be 6 characters or fewer")
		}

		if !strings.HasPrefix(*serialNumber, "B") && !strings.HasPrefix(*serialNumber, "RB") {
			fmt.Println("\033[33msuggestion:\033[0m serial numbers should begin with B for busylight and RB for readerboard units (suggestion only)")
		}

		if strings.ContainsAny(*serialNumber, "$\033") {
			log.Fatal("serial number must not contain '$' or ESC characters")
		}
	}

	var port readerboard.DirectDriver
	log.Printf("Attaching to %s at %d", *portName, *baudRate)
	if err := port.Attach("direct device", *portName, *baudRate, 0); err != nil {
		log.Fatal(err)
	}
	defer port.Detach()

	deviceStatus := probeDevice(port)
	fmt.Println("\nTarget device (current state):")
	describeDevice(deviceStatus)
	if !*force {
		if !util.YorN("Continue", false) {
			os.Exit(0)
		}
	}

	fmt.Println("Programming device...")
	if *serialNumber != "" {
		fmt.Println("\033[1;32mSetting Serial Number...\033[0m")
		if err := port.SendBytes([]byte("\004=#" + *serialNumber + "$#=\004")); err != nil {
			log.Fatal(err)
		}
		time.Sleep(2 * time.Second)
	}

	if *reset {
		fmt.Println("\033[1;32mSetting Default Parameters...\033[0m")
		defSpeed, _ := readerboard.EncodeBaudRateCode(9600)
		if err := port.SendBytes([]byte{4, '=', '.', defSpeed, defSpeed, '?', 4}); err != nil {
			log.Fatal(err)
		}
		time.Sleep(2 * time.Second)
		if deviceStatus.SpeedUSB != 9600 {
			log.Print("Resetting USB baud rate to 9600")
			port.Detach()
			if err := port.Attach("direct device", *portName, 9600, 0); err != nil {
				log.Fatal(err)
			}
			if err := port.SendBytes([]byte{4, 4, 4}); err != nil {
				log.Fatal(err)
			}
			deviceStatus.SpeedUSB = 9600
		}
	}

	if *newAddress != -1 || *newGlobal != 15 || *newUSBbaud != 9600 || *new485baud != 9600 {
		fmt.Println("\033[1;32mSetting New Configuration Parameters...\033[0m")
		bUSB, err := readerboard.EncodeBaudRateCode(*newUSBbaud)
		if err != nil {
			log.Fatal(err)
		}
		b485, err := readerboard.EncodeBaudRateCode(*new485baud)
		if err != nil {
			log.Fatal(err)
		}
		if err := port.SendBytes([]byte{4, '=', readerboard.EncodeInt6(*newAddress), bUSB, b485, readerboard.EncodeInt6(*newGlobal), 4}); err != nil {
			log.Fatal(err)
		}
		time.Sleep(2 * time.Second)
		if deviceStatus.SpeedUSB != *newUSBbaud {
			log.Printf("Resetting USB baud rate to %d", *newUSBbaud)
			port.Detach()
			if err := port.Attach("direct device", *portName, *newUSBbaud, 0); err != nil {
				log.Fatal(err)
			}
			if err := port.SendBytes([]byte{4, 4, 4}); err != nil {
				log.Fatal(err)
			}
		}
	}

	deviceStatus = probeDevice(port)
	fmt.Println("\nTarget device (new state):")
	describeDevice(deviceStatus)

	if *serialNumber != "" {
		if deviceStatus.Serial != *serialNumber {
			log.Fatalf("\033[31mFAILED:\033[m we programmed serial number \"%s\" but the device reported serial number \"%s\"!", *serialNumber, deviceStatus.Serial)
		}
		fmt.Printf("device \033[32mconfirmed\033[0m serial number \"%s\"\n", deviceStatus.Serial)
	}

	if *newAddress != -1 || *newGlobal != 15 || *newUSBbaud != 9600 || *new485baud != 9600 {
		if (*newAddress == -1 && deviceStatus.DeviceAddress != 255) ||
			(*newAddress >= 0 && *newAddress != int(deviceStatus.DeviceAddress)) ||
			*newGlobal != int(deviceStatus.GlobalAddress) ||
			*newUSBbaud != deviceStatus.SpeedUSB ||
			*new485baud != deviceStatus.Speed485 {
			log.Fatalf("\033[31mFAILED:\033[0m device does not show configuration parameters we requested.")
		}
		fmt.Println("device \033[32mconfirmed\033[0m new settings")
	}
}

func describeDevice(deviceDesc any) {
	dev, ok := deviceDesc.(readerboard.DeviceStatus)
	if !ok {
		log.Fatal("Unable to understand device status (internal bug -- returned data type %T is not supported)", deviceDesc)
	}
	fmt.Printf("Target device class:   %s\n", readerboard.ModelClassName(dev.DeviceModelClass))
	if dev.DeviceAddress == 255 {
		fmt.Print("Device RS-485 address: \033[1;31mOFF\033[0m\n")
	} else {
		fmt.Printf("Device RS-485 address: \033[1m%d\033[0m\n", dev.DeviceAddress)
	}
	fmt.Printf("Global RS-485 address: \033[1m%d\033[0m\n", dev.GlobalAddress)
	fmt.Printf("USB Speed:             %d baud\n", dev.SpeedUSB)
	if dev.DeviceAddress == 255 {
		fmt.Printf("RS-485 Speed:          \033[1;31mOFF\033[0m but configured as %d baud\n", dev.Speed485)
	} else {
		fmt.Printf("RS-485 Speed:          %d baud\n", dev.Speed485)
	}
	fmt.Printf("Hardware Revision:     %s\n", dev.HardwareRevision)
	fmt.Printf("Firmware Revision:     %s\n", dev.FirmwareRevision)
	fmt.Printf("Serial Number:         \033[1m%s\033[0m\n", dev.Serial)
}

func probeDevice(port readerboard.DirectDriver) readerboard.DeviceStatus {
	if err := port.SendBytes([]byte{4, 4, 4, 'Q', 4}); err != nil {
		log.Fatal(err)
	}
	receivedData, err := port.Receive()
	if err != nil {
		log.Fatal(err)
	}
	_, parser := readerboard.Query()
	deviceStatus, err := parser(readerboard.UnknownModel, receivedData)
	if err != nil {
		log.Fatal(err)
	}

	if devstat, ok := deviceStatus.(readerboard.DeviceStatus); ok {
		return devstat
	}
	log.Fatalf("Internal bug: device query returned %T value", deviceStatus)
	return readerboard.DeviceStatus{}
}
