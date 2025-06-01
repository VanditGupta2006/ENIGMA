#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

// Number of rotors and alphabet size
#define NUM_ROTORS 3
#define ALPHABET_SIZE 26

// Rotor configurations (historical Enigma I rotors)
char rotors[NUM_ROTORS][ALPHABET_SIZE + 1] = {
    "EKMFLGDQVZNTOWYHXUSPAIBRCJ", // Rotor I
    "AJDKSIRUXBLHWTMCQGZNPYFVOE", // Rotor II
    "BDFHJLCPRTXVZNYEIWGAKMUSQO"  // Rotor III
};

// Reflector configuration (historical Enigma reflector B)
char reflector[ALPHABET_SIZE + 1] = "YRUHQSLDPXNGOKMIEBFZCWVJAT";

// Rotor offset positions (can be modified by user)
volatile int rotor_offsets[NUM_ROTORS] = {0, 0, 0}; // Right, middle, left

// Turnover positions for each rotor (when the next rotor steps)
// Q, E, V for rotors I, II, III respectively (0-indexed)
int turnovers[NUM_ROTORS] = {'Q' - 'A', 'E' - 'A', 'V' - 'A'};

// Plugboard pairings (initialized to no swaps)
int pairings[ALPHABET_SIZE] = {0};

// Input and output buffers
char input_buffer[256] = {0};
char output_buffer[256] = {0};

// To track stepping of rotors
int stepping[NUM_ROTORS] = {0, 0, 0};

// Convert a character to its 0-based index in the alphabet (A=0, B=1, etc.)
int char_to_index(char c) {
    // Return the index of the letter in the alphabet (A=0, B=1, etc.)
    return toupper(c) - 'A';
}

// Convert an index back to a character
char index_to_char(int index) {
    // Convert an index (0-25) back to a character (A-Z)
    return 'A' + (index % ALPHABET_SIZE);
}

// Find the inverse mapping for a given rotor
int index_inverse(int c, int rotor) {
    // This function finds the position in the rotor where the given 
    // character (as index + 'A') appears
    int i;
    for (i = 0; i < ALPHABET_SIZE; i++) {
        if (rotors[rotor][i] == c + 'A') {
            return i;
        }
    }
    return -1; // Error condition
}

// Map an input through a rotor from right to left
int rotor_r_to_l(int input, int rotor) {
    // Apply offset to determine which contact is hit
    int idx = (input + rotor_offsets[rotor]) % ALPHABET_SIZE;
    
    // Determine which contact it maps to, based on the rotor's wiring
    int mapped = rotors[rotor][idx] - 'A';
    
    // Adjust for the offset to get the index of the output contact
    int res = mapped - rotor_offsets[rotor];
    
    // Ensure the result is within range 0-25
    if (res < 0) res += ALPHABET_SIZE;
    if (res >= ALPHABET_SIZE) res -= ALPHABET_SIZE;
    
    return res;
}

// Map an input through a rotor from left to right
int rotor_l_to_r(int input, int rotor) {
    // Apply offset to determine which contact is hit
    int idx = (input + rotor_offsets[rotor]) % ALPHABET_SIZE;
    
    // Find the inverse mapping (which contact on the right maps to this one)
    int inverse = index_inverse(idx, rotor);
    
    // Adjust for the offset to get the index of the output contact
    int res = inverse - rotor_offsets[rotor];
    
    // Ensure the result is within range 0-25
    if (res < 0) res += ALPHABET_SIZE;
    if (res >= ALPHABET_SIZE) res -= ALPHABET_SIZE;
    
    return res;
}

// Map an input through the reflector
int reflect(int input) {
    // The reflector simply uses the lookup table to map the input
    // to its corresponding output
    return reflector[input] - 'A';
}

// Initialize the plugboard with given pairs
void initialize_plugboard(const char *pairs) {
    // Reset the plugboard to no swaps
    for (int i = 0; i < ALPHABET_SIZE; i++) {
        pairings[i] = 0;
    }
    
    // Process each pair
    for (int i = 0; i < strlen(pairs); i += 3) {
        if (pairs[i+1] == ' ' && i+2 < strlen(pairs)) {
            // Extract the two letters to be paired
            int first = toupper(pairs[i]) - 'A';
            int second = toupper(pairs[i+2]) - 'A';
            
            if (first >= 0 && first < ALPHABET_SIZE && 
                second >= 0 && second < ALPHABET_SIZE) {
                // Set up the bidirectional swap
                pairings[first] = second - first;
                pairings[second] = first - second;
            }
        }
    }
}

// Swap a letter through the plugboard
int plug_swap(int input) {
    // Add the offset (which may be 0 if no swap) to the input
    return input + pairings[input];
}

// Advance the rotors
void spin_rotors() {
    // Reset stepping tracking
    for (int i = 0; i < NUM_ROTORS; i++) {
        stepping[i] = 0;
    }
    
    // Check if middle rotor will cause the left rotor to step
    // This implements the "double stepping" mechanism
    for (int i = NUM_ROTORS - 1; i > 0; i--) {
        if (rotor_offsets[i-1] == turnovers[i-1]) {
            rotor_offsets[i] = (rotor_offsets[i] + 1) % ALPHABET_SIZE;
            stepping[i] = 1;
        }
        
        // Additional check for double stepping
        if (rotor_offsets[i] == turnovers[i] && stepping[i] == 0) {
            // Special case for middle rotor (double stepping)
            if (i == 1 && stepping[0] == 1) {
                rotor_offsets[i] = (rotor_offsets[i] + 1) % ALPHABET_SIZE;
                stepping[i] = 1;
            } 
            // Normal case for left rotor
            else if (i == 2) {
                rotor_offsets[i] = (rotor_offsets[i] + 1) % ALPHABET_SIZE;
                stepping[i] = 1;
            }
        }
    }
    
    // The right rotor always advances on every keypress
    rotor_offsets[0] = (rotor_offsets[0] + 1) % ALPHABET_SIZE;
}

// Simplified protothread structure for demonstration
struct pt {
    int yield_flag;
};

#define PT_BEGIN(pt) int yield_state = 0; switch(yield_state) { case 0:
#define PT_END(pt) } return 0;
#define PT_YIELD_TIME_msec(pt, ms) do { yield_state = __LINE__; return 1; case __LINE__: } while(0)
#define PT_EXIT(pt) do { return 0; } while(0)

// Encrypt a message using the PT_Encrypt thread
int PT_Encrypt(struct pt *pt, const char *input, char *output) {
    PT_BEGIN(pt);
    
    int encrypt_idx = 0;
    memset(output, '\0', 256);
    
    while (input[encrypt_idx] != '\0') {
        // Skip non-alphabetic characters
        if (!isalpha(input[encrypt_idx])) {
            output[encrypt_idx] = input[encrypt_idx];
            encrypt_idx++;
            continue;
        }
        
        int res;
        // Apply plugboard to the input character
        res = plug_swap(char_to_index(input[encrypt_idx]));
        
        // Advance the rotors before encryption (once per character)
        spin_rotors();
        
        // Through the rotors from right to left
        res = rotor_r_to_l(res, 0); // Right rotor
        res = rotor_r_to_l(res, 1); // Middle rotor
        res = rotor_r_to_l(res, 2); // Left rotor
        
        // Through the reflector
        res = reflect(res);
        
        // Back through the rotors from left to right
        res = rotor_l_to_r(res, 2); // Left rotor
        res = rotor_l_to_r(res, 1); // Middle rotor
        res = rotor_l_to_r(res, 0); // Right rotor
        
        // Apply plugboard again
        res = plug_swap(res);
        
        // Store the result
        output[encrypt_idx] = index_to_char(res);
        
        encrypt_idx++;
        
        // Yield to allow other threads to run
        PT_YIELD_TIME_msec(pt, 1000);
    }
    
    // Terminate the thread
    PT_EXIT(pt);
    
    PT_END(pt);
}

// Simple encrypt character function (without threading)
char encrypt_char(char c) {
    // If not a letter, return unchanged
    if (!isalpha(c)) return c;
    
    // Convert to uppercase
    c = toupper(c);
    
    // Advance the rotors
    spin_rotors();
    
    // Apply encryption logic
    int res = char_to_index(c);
    res = plug_swap(res);
    res = rotor_r_to_l(res, 0);
    res = rotor_r_to_l(res, 1);
    res = rotor_r_to_l(res, 2);
    res = reflect(res);
    res = rotor_l_to_r(res, 2);
    res = rotor_l_to_r(res, 1);
    res = rotor_l_to_r(res, 0);
    res = plug_swap(res);
    
    return index_to_char(res);
}

// Simple encrypt message function (without threading)
void encrypt_message(const char *input, char *output) {
    int i = 0;
    while (input[i] != '\0') {
        output[i] = encrypt_char(input[i]);
        i++;
    }
    output[i] = '\0';
}

// Print current rotor positions
void print_rotor_status() {
    printf("Rotor positions (L,M,R): %c %c %c\n", 
           index_to_char(rotor_offsets[2]), 
           index_to_char(rotor_offsets[1]), 
           index_to_char(rotor_offsets[0]));
}

// Set rotor positions
void set_rotor_positions(char left, char middle, char right) {
    rotor_offsets[2] = char_to_index(left);
    rotor_offsets[1] = char_to_index(middle);
    rotor_offsets[0] = char_to_index(right);
}

// Example main function to demonstrate usage
int main() {
    char command[10];
    char text[256];
    char plugboard_config[256] = "";
    struct pt pt_encrypt;
    
    printf("=== Enigma Machine Simulator ===\n\n");
    
    while (true) {
        print_rotor_status();
        printf("\nCommands:\n");
        printf("1: Set rotor positions\n");
        printf("2: Set plugboard configuration\n");
        printf("3: Encrypt a message\n");
        printf("4: Quit\n");
        printf("\nEnter command: ");
        
        scanf("%s", command);
        
        if (command[0] == '1') {
            char left, middle, right;
            printf("Enter rotor positions (left middle right, e.g., 'A B C'): ");
            scanf(" %c %c %c", &left, &middle, &right);
            set_rotor_positions(left, middle, right);
            printf("Rotor positions set to: %c %c %c\n\n", left, middle, right);
        }
        else if (command[0] == '2') {
            printf("Enter plugboard pairs (e.g., 'A B C D' to swap A-B and C-D): ");
            getchar(); // Clear newline
            fgets(plugboard_config, sizeof(plugboard_config), stdin);
            initialize_plugboard(plugboard_config);
            printf("Plugboard configuration set\n\n");
        }
        else if (command[0] == '3') {
            printf("Enter message to encrypt: ");
            getchar(); // Clear newline
            fgets(text, sizeof(text), stdin);
            text[strcspn(text, "\n")] = '\0'; // Remove newline
            
            encrypt_message(text, output_buffer);
            printf("Encrypted message: %s\n\n", output_buffer);
            
            // Reset rotor positions for demonstration
            printf("Note: Rotors have advanced during encryption.\n");
        }
        else if (command[0] == '4') {
            printf("Exiting Enigma simulator.\n");
            break;
        }
        else {
            printf("Invalid command. Please try again.\n\n");
        }
    }
    
    return 0;
}
