import pandas as pd
import matplotlib.pyplot as plt

# Load the first CSV file
df1 = pd.read_csv('basic_force_test_no_obstacle.csv')

# Load the second CSV file
df2 = pd.read_csv('basic_force_test_with_obstacle.csv')

# Plot the first data set
plt.plot(df1['TIMESTAMP'], df1['ENCODER_POS_2'], label='Encoder Count with no obstacle')

# Plot the second data set
plt.plot(df2['TIMESTAMP'], df2['ENCODER_POS_2'], label='Encoder Count with obstacle')

# Adding titles and labels
plt.title('Encoder Count with obstacle vs no obstacle')
plt.xlabel('Timestamp (s)')
plt.ylabel('Encoder Count')

# Show the legend to differentiate the lines
plt.legend()

# Display the plot
plt.show()
