#include <iostream>
#include <cstring>      // for memset()
#include <arpa/inet.h>  // for sockaddr_in, inet_pton(), htons()
#include <unistd.h>     // for close(), usleep()
#include <fcntl.h>
#include <sys/mman.h>

// Define the size for GPIO register map
#define GPIO_LEN  0xB4        // GPIO register set length

// GPIO register offsets
#define GPLEV0_OFFSET 0x34    // GPIO Pin Level 0 register offset
#define GPFSEL1_OFFSET 0x04   // GPIO Function Select 1 offset
#define GPPUD_OFFSET   0x94   // GPIO Pull-up/down Register
#define GPPUDCLK0_OFFSET 0x98 // GPIO Pull-up/down Clock Register 0

using namespace std;

int mem_fd;
void *gpio_map;
volatile unsigned int *gpio;

int read_gpio(int pin) {
    int reg = pin / 32;  // Determine which GPLEV register (0 for 0-31, 1 for 32-53)
    int shift = pin % 32; // Determine bit shift for the pin within the register

    // Read the appropriate GPLEV register and mask the specific pin
    unsigned int reg_val = gpio[(GPLEV0_OFFSET / sizeof(unsigned int)) + reg];
    return (reg_val & (1 << shift)) ? 1 : 0;
}

void configure_gpio_as_input(volatile unsigned int *gpio, int pin) {
    int reg = pin / 10;        // Determine which GPFSEL register (0-5)
    int shift = (pin % 10) * 3; // Determine bit shift for the pin within the register

    // Clear the 3 bits corresponding to the GPIO pin
    gpio[reg] &= ~(7 << shift);
}

void set_gpio_pull_down(volatile unsigned int *gpio, int pin) {
    *(gpio + (GPPUD_OFFSET / sizeof(unsigned int))) = 0x01;  // Set control signal to pull-down (01)
    usleep(10);  // Wait 150 cycles (min 5 us), increased to 10 us for safety
    
    *(gpio + (GPPUDCLK0_OFFSET / sizeof(unsigned int))) = (1 << pin);  // Clock the control signal into the pin
    usleep(10);  // Wait 150 cycles (min 5 us), increased to 10 us for safety

    *(gpio + (GPPUD_OFFSET / sizeof(unsigned int))) = 0x00;  // Remove control signal
    *(gpio + (GPPUDCLK0_OFFSET / sizeof(unsigned int))) = 0x00;  // Remove clock
}

bool udp_init(int *client_socket) {
    int ret;
    char ipAddress[14] = "169.254.10.6";  // This is the server IP address
    char localIpAddress[15] = "169.254.164.90"; // Replace with your local interface's IP address
    
    *client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (*client_socket < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }

    // Bind to a specific local IP address (interface)
    sockaddr_in local_address;
    memset(&local_address, 0, sizeof(local_address));
    local_address.sin_family = AF_INET;
    local_address.sin_port = htons(0); // 0 means the OS will choose the port
    inet_pton(AF_INET, localIpAddress, &local_address.sin_addr);

    if (bind(*client_socket, (struct sockaddr*)&local_address, sizeof(local_address)) < 0) {
        std::cerr << "Failed to bind socket to local interface" << std::endl;
        close(*client_socket);
        return false;
    }

    // Set up the server address
    sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(12345);
    inet_pton(AF_INET, ipAddress, &server_address.sin_addr);

    // Connect to the server
    ret = connect(*client_socket, (struct sockaddr*)&server_address, sizeof(server_address));
    if (ret != 0) {
        std::cerr << "Failed to connect to server [" << ipAddress << "]" << std::endl;
        close(*client_socket);
        return false;
    }

    return true;
}

int main(){
    int sock;
    char data[17] = "contact detected";

    if(!udp_init(&sock)){
        cout << "udp init error" << endl;
        return 1;
    } else {
        cout << "udp init success !!" << endl;
    }

    // Open /dev/gpiomem instead of /dev/mem
    if ((mem_fd = open("/dev/gpiomem", O_RDWR | O_SYNC)) < 0) {
        perror("Failed to open /dev/gpiomem");
        return 1;
    }

    // Map GPIO registers into user space
    gpio_map = mmap(
        NULL,                 // Any adddress in our space will do
        GPIO_LEN,             // Map length
        PROT_READ | PROT_WRITE,// Enable reading & writing to mapped memory
        MAP_SHARED,           // Shared with other processes
        mem_fd,               // File to map
        0                     // Offset to GPIO peripheral (always 0 with /dev/gpiomem)
    );

    if (gpio_map == MAP_FAILED) {
        perror("mmap error");
        close(mem_fd);
        return 1;
    }

    close(mem_fd);

    // Cast the mapped memory to an unsigned int pointer for easier access
    gpio = (volatile unsigned int *)gpio_map;

    // Configure GPIO_PIN as input with pull-down resistor
    set_gpio_pull_down(gpio, 17);
    configure_gpio_as_input(gpio, 17);

    printf("Configured GPIO pin 17 as input with pull-down resistor.\n");

    // Main loop to read GPIO pin state
    while (1) {
        if (read_gpio(17) == 1) {
            printf("PIN 17 SET!\n");
            send(sock, data, sizeof(data), 0);
            break;
        }
    }
    
    close(sock); // Properly close the socket after use

    if (munmap(gpio_map, GPIO_LEN) == -1) {
        perror("munmap error");
        return 1;
    }

    printf("Exiting program\n");
    return 0;
}
