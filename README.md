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

Now updated to include and screen (SSD1306 OLED) and buttons to adjust up and down the target temperature. 
