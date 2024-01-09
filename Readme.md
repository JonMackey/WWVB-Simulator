<p align="center">
  <img width="334" height="258" src="WWVB Main.png">
</p>

This STM32CubeIDE project simulates the WWVB signal broadcast from the National Institute of Standards and Technology (NIST) in Fort Collins Colorado.  This signal is used by many clocks to update the time, usually at night.


### Use:

Place the project’s antenna very close to the clock you want to update.  Just let the project run through the night and by morning the clock will update just as if it received the actual WWVB signal.  For a quicker update, temporarily remove the power to the clock.  Most clocks immediately try to update when power is restored.

Another use of this project would be if you want to write your own code to decode the WWVB signal from a WWVB receiver.  Depending on where you live, your WWVB receiver may not work well during the day.  Your WWVB receiver should easily pickup the simulator’s signal when the antennas are placed close together.   And unlike the real WWVB, you control the time being broadcast.  You can simulate any time or date.  This allows you to test various events such as the change to daylight savings time.

And finally the most drastic use would be to replace the WWVB receiver inside your clock with this project and simply use GPS satellite time to update your clock directly.  Most clocks have a separate WWVB receiver board that outputs the demodulated AM WWVB signal.  This project has a debugging output on pin B0 that is a cleaner version of the demodulated signal most WWVB receivers pass to the clock’s mcu.


See my 
[WWVB Simulator](https://www.instructables.com/WWVB-Simulator/) instructable for more information.
