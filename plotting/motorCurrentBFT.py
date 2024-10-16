import pandas as pd
import matplotlib.pyplot as plt

# Load the first CSV file
df1 = pd.read_csv('discard_bad_samples.csv')

# Load the second CSV file
df2 = pd.read_csv('discard_bad_samples.csv')

# Plot the first data set
plt.plot(df1['TIMESTAMP'], df1['MOTOR_CURRENT_2'], label='Motor Current with no obstacle')

# Plot the second data set
plt.plot(df2['TIMESTAMP'], df2['MOTOR_CURRENT_2'], label='Motor Current with obstacle')

# Adding titles and labels
plt.title('Motor Current with obstacle vs no obstacle')
plt.xlabel('Timestamp (s)')
plt.ylabel('Motor Current')

# Show the legend to differentiate the lines
plt.legend()

# Display the plot
plt.show()
