import argparse
import matplotlib.pyplot as plt
import pandas as pd

# Set up argument parser
parser = argparse.ArgumentParser(description='Plot force estimation data (encoder pos/vel and motor state) from two csv file (one of the robot doing a motion in freespace and one in which the robot is performing the same motion but hitting an object).')
parser.add_argument('fileA', type=str, help='Path to the first CSV file (moving in free space))')
parser.add_argument('fileB', type=str, help='Path to the second CSV file (hitting an object)')
parser.add_argument("axis",  type=int, help='Axis of the joint in motion')

# Parse the command-line arguments
args = parser.parse_args()

# Load data from the CSV files
dfA = pd.read_csv(args.fileA)
dfB = pd.read_csv(args.fileB)

# Create a figure and a set of subplots with 3 rows and 1 column
fig, axs = plt.subplots(3, 1, figsize=(8, 10))

# Plot the first pair of data points (Y1 from fileA and Y1 from fileB)
axs[0].plot(dfA['TIMESTAMP'], dfA['ENCODER_POS_' + str(args.axis)], 'r-', label='Encoder Position (no obstacle)')
axs[0].plot(dfB['TIMESTAMP'], dfB['ENCODER_POS_' + str(args.axis)], 'b--', label='Encoder Position (with obstacle)')
axs[0].set_title('Plot 1: Encoder Position')
axs[0].set_ylabel('Encoder Counts')
axs[0].legend()
axs[0].grid(True)

# Plot the second pair of data points (Y2 from fileA and Y2 from fileB)
axs[1].plot(dfA['TIMESTAMP'], dfA['ENCODER_VEL_' + str(args.axis)], 'r-', label='Encoder Velocity (no obstacle)')
axs[1].plot(dfB['TIMESTAMP'], dfB['ENCODER_VEL_' + str(args.axis)], 'b--', label='Encoder Velocity (with obstacle)')
axs[1].set_title('Plot 2: Encoder Velocity')
axs[1].set_ylabel('Encoder Counts/sec')
axs[1].legend()
axs[1].grid(True)

# Plot the third pair of data points (Y1 from fileA and Y2 from fileB)
axs[2].plot(dfA['TIMESTAMP'], dfA['MOTOR_CURRENT_' + str(args.axis)], 'r--', label='Motor Current (no obstacle)')
axs[2].plot(dfB['TIMESTAMP'], dfB['MOTOR_CURRENT_' + str(args.axis)], 'b--', label='Motor Current (with obstacle)')
axs[2].set_title('Plot 3: Motor Current')
axs[2].set_xlabel('Time (sec)')
axs[2].set_ylabel('Motor Current')
axs[2].legend()
axs[2].grid(True)

# Adjust layout to prevent overlap
plt.tight_layout()

# Display the plots
plt.show()
