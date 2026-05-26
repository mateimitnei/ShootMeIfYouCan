#include <Arduino.h>
#include <Servo.h>
#include <TM1637Display.h>

// Pins
#define PIN_BUZZER 2
#define PIN_BUTTON 3
#define PIN_TM_CLK 8
#define PIN_TM_DIO 9
const uint8_t PIN_SERVO[3] = {5, 6, 7};
const uint8_t PIN_LDR[3] = {14, 15, 16};
const uint8_t PIN_LED[3] = {10, 11, 12};

// Objects from included hardware libs
Servo motors[3];
TM1637Display display(PIN_TM_CLK, PIN_TM_DIO);

// Game logic states
enum GameState {
    IDLE_STATE,
    INIT_STATE,
    STANDBY_STATE,
    COUNTDOWN_STATE,
    PLAYING_STATE,
    WIN_STATE,
    LOSS_STATE
};
GameState game_state = IDLE_STATE;

// Individual target states
enum TargetState {
    WAITING_TARGET,
    RISING_TARGET,
    READY_TARGET
};

// Target data
struct Target {
    TargetState state;
    uint32_t reference_light;
    uint32_t time_start_waiting;
    uint32_t current_random_interval;
    uint32_t time_start_rising;
};

Target targets[3];

// Game variables
uint8_t score = 0;
uint8_t misses = 0;
const uint8_t light_difference = 170;

// Timers and counters
uint32_t time_standby_animation = 0;
uint32_t time_start_state = 0;
uint32_t time_last_frame = 0;

uint16_t calibration_samples = 0;
uint8_t current_frame = 0;
uint8_t countdown_value = 3;

// win and loss state variables
uint8_t buzzer_step = 0;
uint8_t score_pos = 99;

bool calibrated = 0;
bool standby_animation_frame = false;

// Initial button state: not pressed
bool btn_state = HIGH;
bool prev_btn_reading = HIGH;
uint32_t last_debounce_time = 0;
const uint8_t debounce_delay = 50;

// Custom display segments
const uint8_t GO_SEGMENTS[] = {
    0x00,
    SEG_A | SEG_C | SEG_D | SEG_E | SEG_F,         // G
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F, // O
    0x00
};
const uint8_t OUTLINE[] = {
    SEG_A | SEG_D | SEG_E | SEG_F,
    SEG_A | SEG_D,
    SEG_A | SEG_D,
    SEG_A | SEG_B | SEG_C | SEG_D
};

// Loss frame 1 is just a '0' so I'm using the showNumberDec function for it.
// Custom data for the next two frames for the loss animation:
const uint8_t LOSS_FRAME_2[] = {
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F, // '0' at pos 0
    SEG_C | SEG_D | SEG_E | SEG_G,                 // lower 'o' at pos 1
    0x00, 
    0x00
};

const uint8_t LOSS_FRAME_3[] = {
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F, // '0' at pos 0
    SEG_C | SEG_D | SEG_E | SEG_G,                 // lower 'o' at pos 1
    SEG_D,                                         // '_' (bottom segment) at pos 2
    0x00
};

// variables for uart communication with serial monitor
uint32_t last_print_time[3] = {0, 0, 0};


// --- Utility functions --- //


void lower_all_targets() {
    for (int i = 0; i < 3; i++) {
        motors[i].write(0);
        digitalWrite(PIN_LED[i], LOW);
    }
}

void raise_all_targets() {
    for (int i = 0; i < 3; i++) {
        motors[i].write(90);
    }
}

void display_score(int score) {
    display.clear();
    if (score < 10) {
        display.showNumberDec(score, false, 1, 1);
    } else {
        display.showNumberDec(score, false, 2, 1);
    }
}

void display_init_animation(uint8_t frame) {
    uint8_t data[4];
    memcpy(data, OUTLINE, sizeof(OUTLINE));

    for (int i = 0; i < 2; i++) {
        uint8_t off_segment = (frame + i) % 12;
        switch(off_segment) {
            case 0:    data[0] &= ~SEG_A; break;
            case 1:    data[1] &= ~SEG_A; break;
            case 2:    data[2] &= ~SEG_A; break;
            case 3:    data[3] &= ~SEG_A; break;
            case 4:    data[3] &= ~SEG_B; break;
            case 5:    data[3] &= ~SEG_C; break;
            case 6:    data[3] &= ~SEG_D; break;
            case 7:    data[2] &= ~SEG_D; break;
            case 8:    data[1] &= ~SEG_D; break;
            case 9:    data[0] &= ~SEG_D; break;
            case 10:   data[0] &= ~SEG_E; break;
            case 11:   data[0] &= ~SEG_F; break;
        }
    }
    display.setSegments(data);
}

void game_init() {
    for (int i = 0; i < 3; i++) {
        targets[i].state = WAITING_TARGET;
        targets[i].reference_light = 0;
        targets[i].time_start_waiting = 0;
        targets[i].current_random_interval = 0;
        targets[i].time_start_rising = 0;
        digitalWrite(PIN_LED[i], LOW);
    }
    game_state = INIT_STATE;
    misses = 0;
    score = 0;
    calibrated = 0;
    calibration_samples = 0;
    time_start_state = millis();
    raise_all_targets();
}

void lower_target(int i, uint32_t now) {
    motors[i].write(0);
    digitalWrite(PIN_LED[i], LOW);
    targets[i].current_random_interval = random(1000, 4000);
    targets[i].time_start_waiting = now;
    targets[i].state = WAITING_TARGET;
}


// --- State handlers --- //

void check_button_press(uint32_t now) {
    bool btn_reading = digitalRead(PIN_BUTTON);

    // Debounce check
    if (btn_reading != prev_btn_reading) {
        last_debounce_time = now;
        prev_btn_reading = btn_reading;
    }

    if ((now - last_debounce_time) > debounce_delay) {
        if (btn_reading != btn_state) {
            btn_state = btn_reading;

            // Pressed button:
            if (btn_state == LOW) {
                // Pressed to start playing:
                if (game_state == STANDBY_STATE) {
                    lower_all_targets();
                    time_start_state = 0;
                    game_state = COUNTDOWN_STATE;
                }
                // Pressed to (re)start the game and calibrate sensors:
                else {
                    tone(PIN_BUZZER, 500, 300);
                    game_init();
                    time_start_state = now;
                }
            }
        }
    }
}


void init_state(uint32_t now) {
    // Animation
    if (now - time_last_frame > 80) {
        time_last_frame = now;
        display_init_animation(current_frame);
        current_frame = (current_frame + 1) % 12;
    }

    uint32_t elapsed = now - time_start_state;

    // Sample for a second, after the first second of animation
    if (elapsed >= 1000 && elapsed < 2000) {
        for (int i = 0; i < 3; i++) {
            targets[i].reference_light += analogRead(PIN_LDR[i]);
        }
        calibration_samples++;
    }

    // Mean values
    if (elapsed >= 2000 && calibrated == 0) {
        for (int i = 0; i < 3; i++) {
            targets[i].reference_light /= calibration_samples;
            Serial.print("Target ");
            Serial.print(i);
            Serial.print(" reference light: ");
            Serial.println(targets[i].reference_light);
        }
        calibrated = 1;
    }

    // Enter STANDBY_STATE after 3 seconds of INIT_STATE
    if (elapsed >= 3000) {
        game_state = STANDBY_STATE;
    }
}

void standby_state(uint32_t now) {
    if (now - time_standby_animation > 500) {
        time_standby_animation = now;
        standby_animation_frame = !standby_animation_frame;

        if (standby_animation_frame) {
            display.showNumberDec(0, true, 4, 0);
        } else {
            display.clear();
        }
    }
}

void countdown_state(uint32_t now) {
    // Start countdown with '3'
    if (time_start_state == 0) {
        time_start_state = now;
        countdown_value = 3;
        display.clear();
        display.showNumberDec(countdown_value, false, 1, 1);
        tone(PIN_BUZZER, 800, 100);
        return;
    }

    // '2' and '1'
    if (countdown_value > 1 && now - time_start_state >= 700) {
        countdown_value--;
        time_start_state = now;
        display.clear();
        display.showNumberDec(countdown_value, false, 1, 1);
        tone(PIN_BUZZER, 800, 100);
        return;
    }

    // 'GO'
    if (countdown_value == 1 && now - time_start_state >= 700) {
        countdown_value = 0;
        time_start_state = now;
        display.setSegments(GO_SEGMENTS);
        tone(PIN_BUZZER, 1200, 500);
        return;
    }

    // Move to PLAYING_STATE after 'GO'
    if (countdown_value == 0 && now - time_start_state >= 1000) {
        display_score(score);
        for (int i = 0; i < 3; i++) {
            targets[i].state = WAITING_TARGET;
            targets[i].current_random_interval = random(500, 4000);
            targets[i].time_start_waiting = now;
        }
        game_state = PLAYING_STATE;
        time_start_state = 0;
    }
}

void playing_state(uint32_t now) {
    for (int i = 0; i < 3; i++) {
        // Target in horizontal position
        if (targets[i].state == WAITING_TARGET) {
            if (now - targets[i].time_start_waiting > targets[i].current_random_interval) {
                motors[i].write(90);
                digitalWrite(PIN_LED[i], HIGH);
                targets[i].time_start_rising = now;
                targets[i].state = RISING_TARGET;
            }
        }
        // Target moving to vertical position
        else if (targets[i].state == RISING_TARGET && now - targets[i].time_start_rising > 300) {
            targets[i].state = READY_TARGET;
        }
        // Target in vertical position
        else if (targets[i].state == READY_TARGET) {
            // Read light level
            uint32_t current_light = analogRead(PIN_LDR[i]);
            // Serial monitor debugging:
            if (now - last_print_time[i] > 500) {
                Serial.print("Target "); Serial.print(i);
                Serial.print(" light: "); Serial.println(current_light);
                last_print_time[i] = now;
            }

            // Check if target has been hit
            if (current_light > (targets[i].reference_light + light_difference)) {
                lower_target(i, now);
                display_score(score);
                tone(PIN_BUZZER, 1200, 100);

                // Serial monitor debugging:
                Serial.print("HIT "); Serial.print(i);
                Serial.print(" light: "); Serial.println(current_light);
                last_print_time[i] = now;

                score++;
                if (score >= 10) {
                    game_state = WIN_STATE;
                    lower_all_targets();
                    buzzer_step = 0;
                    time_start_state = now;
                    break;
                }
            }

            // Check if target has been up for too long
            if (now - targets[i].time_start_rising > 3000) {
                misses++;
                lower_target(i, now);
                tone(PIN_BUZZER, 200, 100);
                Serial.print("MISSED TARGET "); Serial.print(i);
                Serial.print(" -> Misses: "); Serial.println(misses);
                
                if (misses >= 3) {
                    game_state = LOSS_STATE;
                    lower_all_targets();
                    buzzer_step = 0;
                    time_start_state = now;
                    break;
                }
            }
        }
    }
}

void win_state(uint32_t now) {
    uint32_t elapsed_total = now - time_start_state;

    // Buzzer notes
    if (buzzer_step == 0) { tone(PIN_BUZZER, 800, 150); buzzer_step++; }
    else if (buzzer_step == 1 && elapsed_total > 150) { buzzer_step++; }
    else if (buzzer_step == 2 && elapsed_total > 300) { tone(PIN_BUZZER, 600, 150); buzzer_step++; }
    else if (buzzer_step == 3 && elapsed_total > 450) { buzzer_step++; }
    else if (buzzer_step == 4 && elapsed_total > 600) { tone(PIN_BUZZER, 1200, 300); buzzer_step++; }

    // Animation loop
    uint8_t next_score_pos = score_pos;

    if (elapsed_total < 200 || elapsed_total > 6900) {
        next_score_pos = 1; // Middle
    } else {
        uint32_t elapsed_loop = (elapsed_total - 200) % 670;

        if (elapsed_loop < 300) {
            next_score_pos = 0; // Left
        }
        else if (elapsed_loop < 335) {
            next_score_pos = 1; // Middle
        }
        else if (elapsed_loop < 635) {
            next_score_pos = 2; // Right
        }
        else {
            next_score_pos = 1; // Middle
        }
    }

    // Modify display only if position has changed
    if (score_pos != next_score_pos) {
        display.clear();
        display.showNumberDec(10, false, 2, next_score_pos);
        score_pos = next_score_pos;
    }
}

void loss_state(uint32_t now) {
    uint32_t elapsed_total = now - time_start_state;

    // Buzzer and display stages
    if (buzzer_step == 0) {
        display.clear();
        tone(PIN_BUZZER, 1000, 200);
        display.showNumberDec(0, false, 1, 0);
        buzzer_step++; 
    }
    else if (buzzer_step == 1 && elapsed_total > 200) { buzzer_step++; }
    else if (buzzer_step == 2 && elapsed_total > 400) { 
        tone(PIN_BUZZER, 800, 200); 
        display.setSegments(LOSS_FRAME_2);
        buzzer_step++; 
    }
    else if (buzzer_step == 3 && elapsed_total > 600) { buzzer_step++; }
    else if (buzzer_step == 4 && elapsed_total > 800) { 
        tone(PIN_BUZZER, 600, 200);
        display.setSegments(LOSS_FRAME_3);
        buzzer_step++;
    }
    else if (buzzer_step == 5 && elapsed_total > 1000) { buzzer_step++; }
    else if (buzzer_step == 6 && elapsed_total > 1200) { 
        tone(PIN_BUZZER, 400, 400); 
        display.clear();
        buzzer_step++; 
    }
}

// --- Setup and loop --- //

void setup() {
    Serial.begin(9600);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(PIN_BUZZER, OUTPUT);

    for (int i = 0; i < 3; i++) {
        motors[i].attach(PIN_SERVO[i]);
        pinMode(PIN_LED[i], OUTPUT);
    }

    display.setBrightness(0x09);
    display.clear();
    randomSeed(analogRead(A5)); // Generates seed from noise
}

void loop() {
    uint32_t now = millis();

    check_button_press(now);

    switch (game_state) {
        case INIT_STATE: init_state(now); break;
        case STANDBY_STATE: standby_state(now); break;
        case COUNTDOWN_STATE: countdown_state(now); break;
        case PLAYING_STATE: playing_state(now); break;
        case WIN_STATE: win_state(now); break;
        case LOSS_STATE: loss_state(now); break;
        default: break;
    }
}
