import pandas as pd
import matplotlib.pyplot as plt

# Load the first CSV file
df1 = pd.read_csv('try.csv')

# Plot the first data set
plt.plot(df1['TIMESTAMP'], df1['MOTOR_CURRENT_2'], label='Motor Current with no obstacle')

# Adding titles and labels
plt.title('Motor Current with obstacle vs no obstacle')
plt.xlabel('Timestamp (s)')
plt.ylabel('Motor Current')

# Show the legend to differentiate the lines
plt.legend()

# Display the plot
plt.show()
