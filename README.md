![GitHub All Releases](https://img.shields.io/github/downloads/maccoylton/homekit_wifi_thermostat/total)

# homekit_wifi_thermostat

A thermostat accessory for remotely controlling central heating.
This provides a thermostat accessory, You also need a swith accessory and to set up event trigers in the Eve app to make the 
connection between the thermostat and the switch, hence it's called wifi thermostat as the connection between the thermostat 
accessory and your central heating device is wireless.

It uses a DH22 to sense the temperature and sets the current_heating_cooling_status based on the other settings of the 
accessory. It sends events as the value changes, which can then be actioned by triggers using the Eve app.

For example when the thermostat sets the current_heating_cooling_status to heat/off, use Eve to trigger the switch accesory 
to on/off respectively. You can also set timer based events in Eve to control the target_heating_cooling_status, in other 
words you can create a program of when you want the heating to go on or off. 

Now updated to include a screen (SSD1306 OLED) and buttons to adjust up and down the target temperature. 

The screen will illuminate during the inital setup and on power up. And then wil go dark afetr aproxiamtely 10 seconds, it will come on when you press the up or down button and then go off again approxiamtely 10 seconds afetr the last button press.

GPIO used are as follows:- 

  4 - DH22

  12 - Temperature Up (button connect to ground)

  13 - Temperature Down (button conected to ground) 
  
  0 - Reset (switch connected to ground) 
  
  2 - LED 
  
  14 - SSD1306 SCL_PIN
  
  5 - SSD1306 SDA_PIN
