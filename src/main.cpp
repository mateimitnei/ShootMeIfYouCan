#include <Arduino.h>
#include <Servo.h>
#include <TM1637Display.h>

// Pins
const uint8_t PIN_BUZZER = 2;
const uint8_t PIN_BUTTON = 3;
const uint8_t PIN_SERVO[3] = {5, 6, 7};
const uint8_t PIN_LDR[3] = {A0, A1, A2};
const uint8_t PIN_LED[3] = {10, 11, 12};
const uint8_t PIN_TM_CLK = 8;
const uint8_t PIN_TM_DIO = 9;

// Objects from included hardware libs
Servo motors[3];
TM1637Display display(PIN_TM_CLK, PIN_TM_DIO);

// Game logic states
enum GameState {
    INIT_STATE,
    STANDBY_STATE,
    COUNTDOWN_STATE,
    PLAYING_STATE,
    WIN_STATE
};
GameState game_state = INIT_STATE;

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
const uint8_t light_threshold = 150;

// Timers and counters
uint32_t time_standby_animation = 0;
uint32_t time_start_init = 0;
uint32_t time_last_frame = 0;
uint32_t time_start_countdown = 0;

uint8_t calibrated = 0;
uint16_t calibration_samples = 0;
uint8_t current_frame = 0;
uint8_t countdown_value = 3;

// Win state variables
uint32_t time_start_win = 0;
uint8_t buzzer_win_step = 0;
uint8_t score_pos = 99;

bool standby_animation_frame = false;
// Initial button states: not pressed
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
                // Pressed to start the game:
                if (game_state == STANDBY_STATE) {
                    lower_all_targets();
                    time_start_countdown = 0;
                    game_state = COUNTDOWN_STATE;
                }
                // Pressed to restart the game:
                else if (game_state != INIT_STATE) {
                    tone(PIN_BUZZER, 500, 300);
                    lower_all_targets();
                    calibrated = 0;
                    current_frame = 0;
                    raise_all_targets();
                    time_start_init = now;
                    game_state = INIT_STATE;
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

    uint32_t elapsed = now - time_start_init;

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
    if (time_start_countdown == 0) {
        time_start_countdown = now;
        countdown_value = 3;
        display.clear();
        display.showNumberDec(countdown_value, false, 1, 1);
        tone(PIN_BUZZER, 800, 100);
        return;
    }

    // '2' and '1'
    if (countdown_value > 1 && now - time_start_countdown >= 700) {
        countdown_value--;
        time_start_countdown = now;
        display.clear();
        display.showNumberDec(countdown_value, false, 1, 1);
        tone(PIN_BUZZER, 800, 100);
        return;
    }

    // 'GO'
    if (countdown_value == 1 && now - time_start_countdown >= 700) {
        countdown_value = 0;
        time_start_countdown = now;
        display.setSegments(GO_SEGMENTS);
        tone(PIN_BUZZER, 1200, 500);
        return;
    }

    // Move to PLAYING_STATE after 'GO'
    if (countdown_value == 0 && now - time_start_countdown >= 1000) {
        score = 0;
        display_score(score);
        for (int i = 0; i < 3; i++) {
            targets[i].state = WAITING_TARGET;
            targets[i].current_random_interval = random(500, 4000);
            targets[i].time_start_waiting = now;
        }
        game_state = PLAYING_STATE;
        time_start_countdown = 0;
    }
}

void playing_state(uint32_t now) {
    for (int i = 0; i < 3; i++) {
        // Target in horizontal position
        if (targets[i].state == WAITING_TARGET) {
            if (now - targets[i].time_start_waiting > targets[i].current_random_interval) {
                motors[i].write(90); digitalWrite(PIN_LED[i], HIGH);
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
            if (analogRead(PIN_LDR[i]) > (targets[i].reference_light + light_threshold)) {
                score++;
                display_score(score);
                motors[i].write(0);
                digitalWrite(PIN_LED[i], LOW);
                tone(PIN_BUZZER, 1200, 100);

                if (score >= 10) {
                    game_state = WIN_STATE;
                    lower_all_targets();
                    buzzer_win_step = 0;
                    time_start_win = now;
                    break;
                } else {
                    targets[i].current_random_interval = random(1000, 4000);
                    targets[i].time_start_waiting = now;
                    targets[i].state = WAITING_TARGET;
                }
            }
        }
    }
}


void win_state(uint32_t now) {
    uint32_t elapsed_total = now - time_start_win;

    // Buzzer notes
    if (buzzer_win_step == 0) { tone(PIN_BUZZER, 800, 150); buzzer_win_step++; }
    else if (buzzer_win_step == 1 && elapsed_total > 150) { buzzer_win_step++; }
    else if (buzzer_win_step == 2 && elapsed_total > 300) { tone(PIN_BUZZER, 600, 150); buzzer_win_step++; }
    else if (buzzer_win_step == 3 && elapsed_total > 450) { buzzer_win_step++; }
    else if (buzzer_win_step == 4 && elapsed_total > 600) { tone(PIN_BUZZER, 1200, 300); buzzer_win_step++; }

    // Animation
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


// --- Setup and loop --- //

void setup() {
    Serial.begin(9600);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(PIN_BUZZER, OUTPUT);

    for (int i = 0; i < 3; i++) {
        motors[i].attach(PIN_SERVO[i]);
        pinMode(PIN_LED[i], OUTPUT);
        digitalWrite(PIN_LED[i], LOW);
    }

    // Init data for the targets
    for (int i = 0; i < 3; i++) {
        targets[i].state = WAITING_TARGET;
        targets[i].reference_light = 0;
        targets[i].time_start_waiting = 0;
        targets[i].current_random_interval = 0;
        targets[i].time_start_rising = 0;
    }

    display.setBrightness(0x01);
    randomSeed(analogRead(A5)); // Generates seed from noise
    game_state = INIT_STATE;
    raise_all_targets();
    time_start_init = millis();
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
    }
}