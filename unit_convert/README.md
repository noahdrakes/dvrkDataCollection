# Data Conversion

This subfolder contains a Python script to convert bits (raw data) into SI unit values. Note that the units for torques are N*m, for positions are rad or m, and for velocities are rad/s or m/s.

## Running 

- Example command:

```
         python unit_convert.py -c sawRobotIO1394-PSM1-26611.xml.json -f capture_Mon_Dec__9_14_07_18_2024.csv -g Classic
```

## Output

The program will output a csv file for each capture containing the following data for each axis:

*Timestamp,* *TorqueFeedback1*,..,*TorqueFeedbackN*, *TorqueCommand1*, *TorqueCommandN*, *PositionFeedback1*, *PositionFeedbackN*, *VelocityFeedback1*, *VelocityFeedbackN* in that order.

The filename for each capture is capture_[date and time]_unitConvert.csv

###### Contact Info
Send me an email if you have any questions.
Simon Hao
email: hao.yang@vanderbilt.edu
