const unsigned long default_chopper_config = 0
| (2<<15) // comparator blank time 2=34
| _BV(13) //random t_off
| (3 << 7) //hysteresis end time
| (5 << 4) // hysteresis start time
| 5 //t OFF
;
//the endstop config seems to be write only 
unsigned long endstop_config_shadow[2] = {
  _BV(11), _BV(11)};

volatile boolean tmc5041_read_position_read_status = true;
volatile boolean v_sense_high[2] = {true, true};

int set_currents[2];

TMC5041_motion_info tmc5031_next_movement[2];

extern char active_motors;

void prepareTMC5041() {
  //configure the cs pin
  pinMode(CS_5041_PIN, OUTPUT);
  digitalWrite(CS_5041_PIN, LOW);
  // configure the interrupt pin
  pinMode(INT_5041_PIN, INPUT);
  digitalWrite(INT_5041_PIN, LOW);
  //enable the pin change interrupts
  PCICR |= _BV(0); //PCIE0 - there are no others 
  PCMSK0 = _BV(4); //we want jsut to lsiten to the INT 5031
  writeRegister(TMC5041_MOTORS, TMC5041_GENERAL_CONFIG_REGISTER, _BV(3)); //int/PP are outputs
}

void initialzeTMC5041() {

  //get rid of the 'something happened after reboot' warning
  readRegister(TMC5041_MOTORS, TMC5041_GENERAL_STATUS_REGISTER);
  writeRegister(TMC5041_MOTORS, TMC5041_GENERAL_CONFIG_REGISTER, _BV(3)); //int/PP are outputs
  // motor #1
  writeRegister(TMC5041_MOTORS, TMC5041_RAMP_MODE_REGISTER_1,0); //enforce positioing mode
  writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_1,0);
  writeRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_1,0);
  setCurrentTMC5041(0,DEFAULT_CURRENT_IN_MA);
  writeRegister(TMC5041_MOTORS, TMC5041_CHOPPER_CONFIGURATION_REGISTER_1,default_chopper_config);

  // motor #2
  //get rid of the 'something happened after reboot' warning
  writeRegister(TMC5041_MOTORS, TMC5041_RAMP_MODE_REGISTER_2,0); //enforce positioing mode
  writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_2,0);
  writeRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_2,0);
  setCurrentTMC5041(1,DEFAULT_CURRENT_IN_MA);
  writeRegister(TMC5041_MOTORS, TMC5041_CHOPPER_CONFIGURATION_REGISTER_2,default_chopper_config);

  //configure reference switches (to nothing)
  writeRegister(TMC5041_MOTORS, TMC5041_REFERENCE_SWITCH_CONFIG_REGISTER_1, 0x0);
  writeRegister(TMC5041_MOTORS, TMC5041_REFERENCE_SWITCH_CONFIG_REGISTER_2, 0x0);

  //those values are static and will anyway reside in the tmc5041 settings
  writeRegister(TMC5041_MOTORS, TMC5041_A_1_REGISTER_1,0);
  writeRegister(TMC5041_MOTORS, TMC5041_D_1_REGISTER_1,1); //the datahseet says it is needed
  writeRegister(TMC5041_MOTORS, TMC5041_V_START_REGISTER_1, 0);
  writeRegister(TMC5041_MOTORS,TMC5041_V_STOP_REGISTER_1,1); //needed acc to the datasheet?
  writeRegister(TMC5041_MOTORS, TMC5041_V_1_REGISTER_1,0);
  writeRegister(TMC5041_MOTORS, TMC5041_A_1_REGISTER_2,0);
  writeRegister(TMC5041_MOTORS, TMC5041_D_1_REGISTER_2,1); //the datahseet says it is needed
  writeRegister(TMC5041_MOTORS, TMC5041_V_START_REGISTER_2, 0);
  writeRegister(TMC5041_MOTORS,TMC5041_V_STOP_REGISTER_2,1); //needed acc to the datasheet?
  writeRegister(TMC5041_MOTORS, TMC5041_V_1_REGISTER_2,0);

  //and ensure that the event register are emtpy 
  readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_1);
  readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_2);
}

int getCurrentTMC5041(unsigned char motor_number) {
  if (motor_number > 1) {
    return -1;
  }
  return set_currents[motor_number];
}

const char setCurrentTMC5041(unsigned char motor_number, int newCurrent) {
#ifdef DEBUG_MOTOR_CONTFIG
  Serial.print(F("Settings current for TMC5041 #"));
  Serial.print(motor_number);
  Serial.print(F(" to "));
  Serial.print(newCurrent);
  Serial.println(F("mA."));
#endif

  unsigned char run_current = calculateCurrentValue(motor_number, newCurrent);
  unsigned char hold_current = run_current;
  unsigned long current_register=0;

  //set the holding delay
  current_register  = I_HOLD_DELAY;
  current_register <<= 8;
  current_register |= run_current;
  current_register <<= 8;
  current_register |= hold_current;
  if (motor_number==0) {
    writeRegister(TMC5041_MOTORS, TMC5041_HOLD_RUN_CURRENT_REGISTER_1,current_register);
  } else {
    writeRegister(TMC5041_MOTORS, TMC5041_HOLD_RUN_CURRENT_REGISTER_2,current_register);
  }

  set_currents[motor_number] = (int)(((run_current+1.0)/32.0)*((v_sense_high[motor_number]?V_HIGH_SENSE:V_LOW_SENSE)/TMC_5041_R_SENSE)*(1000.0/SQRT_2));

  unsigned long chopper_config = 0
    | (2ul << 15ul) // comparator blank time 2=34
    | (1ul << 13ul) // random t_off
    | (3ul << 7ul)  // hysteresis end time
    | (5ul << 4ul)  // hysteresis start time
    | 5             // t OFF
    ;
  if (v_sense_high[motor_number]) {
    chopper_config |=  (1ul<<17ul); // lower v_sense voltage
  } else {
    chopper_config &= ~(1ul<<17ul); // higher v_sense voltage
  } 
  if (motor_number==0) {
    writeRegister(TMC5041_MOTORS, TMC5041_CHOPPER_CONFIGURATION_REGISTER_1,chopper_config);
  } else {
    writeRegister(TMC5041_MOTORS, TMC5041_CHOPPER_CONFIGURATION_REGISTER_2,chopper_config);
  }
  return 0;
}

const char configureEndstopTMC5041(unsigned char motor_nr, boolean left, boolean active, boolean active_high) {
#ifdef DEBUG_ENDSTOPS_DETAIL
  Serial.print(F("Enstop config before "));
  Serial.println(endstop_config_shadow[motor_nr],HEX);

#endif
  unsigned long endstop_config = getClearedEndstopConfigTMC5041(motor_nr, left);
#ifdef DEBUG_ENDSTOPS_DETAIL
  Serial.print(F("Cleared enstop config before "));
  Serial.println(endstop_config,HEX);
#endif

  if (active) {
    if (left) {
      endstop_config |= _BV(0);
      if (active_high) {
#ifdef DEBUG_ENDSTOPS
        Serial.print(F("TMC5041 motor "));
        Serial.print(motor_nr);
        Serial.println(F(" - configuring left end stop as active high"));
#endif
        endstop_config |= _BV(5); //nothing to do here ...
      }  
      else {
#ifdef DEBUG_ENDSTOPS
        Serial.print(F("TMC5041 motor "));
        Serial.print(motor_nr);
        Serial.println(F(" - configuring left end stop as active low"));
#endif
        endstop_config |= _BV(2) | _BV(5);
      }
    } 
    else {
      endstop_config |= _BV(1);
      if (active_high) {
#ifdef DEBUG_ENDSTOPS
        Serial.print(F("TMC5041 motor "));
        Serial.print(motor_nr);
        Serial.println(F(" - configuring right end stop as active high"));
#endif
        endstop_config |= _BV(7);
      }  
      else {
#ifdef DEBUG_ENDSTOPS
        Serial.print(F("TMC5041 motor "));
        Serial.print(motor_nr);
        Serial.println(F(" - configuring right end stop as active low"));
#endif
        endstop_config |= _BV(3) | _BV(7);
      }
    }
  }
#ifdef DEBUG_ENDSTOPS_DETAIL
  Serial.print(F("New enstop config "));
  Serial.println(endstop_config,HEX);
#endif
  if (motor_nr == 0) {
    writeRegister(TMC5041_MOTORS,TMC5041_REFERENCE_SWITCH_CONFIG_REGISTER_1,endstop_config);
  } 
  else {
    writeRegister(TMC5041_MOTORS,TMC5041_REFERENCE_SWITCH_CONFIG_REGISTER_2,endstop_config);
  }
  return 0;
}

const char configureVirtualEndstopTMC5041(unsigned char motor_nr, boolean left, long positions) {
  //todo also with active
  return NULL; //TODO this has to be implemented ...
}

unsigned char calculateCurrentValue(byte motor, int current) {
  // I_RMS = ((CS+1)/32)*(V_FS/R_SENSE)*(1/SQRT(2))
  // CS = ((I_RMS*32*SQRT(2)*R_SENSE)/(V_FS))-1
  float i_rms = current/1000.0;
  float _cs;
  _cs = ((i_rms * 32 * SQRT_2 * TMC_5041_R_SENSE)/V_HIGH_SENSE)-0.5;
  v_sense_high[motor] = true;
  if (_cs > 31) {
    _cs = ((i_rms * 32 * SQRT_2 * TMC_5041_R_SENSE)/V_LOW_SENSE)-0.5;
    v_sense_high[motor] = false;
    if (_cs > 31) {
      return 31;
    } else if (_cs < 0) {
      return 0;
    } else {
      return (unsigned char)_cs;
    }
  } else if (_cs < 0) {
    return 0;
  } else {
    return (unsigned char)_cs;
  }
}

void  moveMotorTMC5041(char motor_nr, long target_pos, double vMax, double aMax, boolean isWaypoint) {
#ifdef DEBUG_MOTION_SHORT
  Serial.print('M');
  Serial.print(motor_nr,DEC);
  Serial.print(F(" t "));
  Serial.print(target_pos);
  Serial.print(F(" @ "));
  Serial.println(vMax);
#endif
#ifdef DEBUG_MOTION
  Serial.print(F("5041 #1 is going to "));
  Serial.print(tmc5031_next_movement[0].target,DEC);
  Serial.print(F(" @ "));
  Serial.println(tmc5031_next_movement[0].vMax,DEC);
#endif
  tmc5031_next_movement[motor_nr].target = target_pos;
  tmc5031_next_movement[motor_nr].vMax = vMax;
  tmc5031_next_movement[motor_nr].aMax = aMax;
}

void tmc5041_prepare_next_motion() {
  if (tmc5031_next_movement[0].vMax!=0) {
    writeRegister(TMC5041_MOTORS, TMC5041_A_MAX_REGISTER_1,tmc5031_next_movement[0].aMax);
    writeRegister(TMC5041_MOTORS, TMC5041_D_MAX_REGISTER_1,tmc5031_next_movement[0].aMax);

    writeRegister(TMC5041_MOTORS,TMC5041_V_MAX_REGISTER_1, tmc5031_next_movement[0].vMax);
    writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_1,tmc5031_next_movement[0].target);
    active_motors |= (1<<nr_of_coordinated_motors);
    tmc5031_next_movement[0].vMax=0;
  }
  if (tmc5031_next_movement[1].vMax!=0) {
#ifdef DEBUG_MOTION
    Serial.print(F("5041 #2 is going to "));
    Serial.print(tmc5031_next_movement[1].target,DEC);
    Serial.print(F(" @ "));
    Serial.println(tmc5031_next_movement[1].vMax,DEC);
#endif
    writeRegister(TMC5041_MOTORS, TMC5041_A_MAX_REGISTER_2,tmc5031_next_movement[1].aMax);
    writeRegister(TMC5041_MOTORS, TMC5041_D_MAX_REGISTER_2,tmc5031_next_movement[1].aMax);

    writeRegister(TMC5041_MOTORS,TMC5041_V_MAX_REGISTER_2, tmc5031_next_movement[1].vMax);
    writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_2,tmc5031_next_movement[1].target);
    active_motors |= (1<<(nr_of_coordinated_motors+1));
    tmc5031_next_movement[1].vMax=0;
  }
}

const char homeMotorTMC5041(unsigned char motor_nr, unsigned long timeout, 
double homing_fast_speed, double homing_low_speed, long homing_retraction,
double homming_accel,
char* followers)
{
  //TODO shouldn't we check if there is a motion going on??
  unsigned long acceleration_value = 1000;// min((unsigned long) homming_accel,0xFFFFul);

#ifdef DEBUG_HOMING
  Serial.print(F("Homing for motor "));
  Serial.print(motor_nr,DEC);
  Serial.print(F(", timeout="));
  Serial.print(timeout);
  Serial.print(F(", fast="));
  Serial.print(homing_fast_speed);
  Serial.print(F(", slow="));
  Serial.print(homing_low_speed);
  Serial.print(F(", retract="));
  Serial.print(homing_retraction);
  Serial.print(F(", aMax="));
  Serial.print(acceleration_value);
  Serial.print(F(": follow=("));
  for (char i = 0; i< homing_max_following_motors ;i++) {
    Serial.print(followers[i],DEC);
    Serial.print(F(", "));
  }
  Serial.print(')');
  Serial.println();
#endif
  //configure acceleration for homing
  writeRegister(TMC5041_MOTORS, TMC5041_A_MAX_REGISTER_1,acceleration_value);
  writeRegister(TMC5041_MOTORS, TMC5041_D_MAX_REGISTER_1,acceleration_value);
  writeRegister(TMC5041_MOTORS, TMC5041_A_1_REGISTER_1,acceleration_value);
  writeRegister(TMC5041_MOTORS, TMC5041_D_1_REGISTER_1,acceleration_value); //the datahseet says it is needed
  writeRegister(TMC5041_MOTORS, TMC5041_V_START_REGISTER_1, 0);
  writeRegister(TMC5041_MOTORS,TMC5041_V_STOP_REGISTER_1,1); //needed acc to the datasheet?
  writeRegister(TMC5041_MOTORS, TMC5041_V_1_REGISTER_1,0);

  writeRegister(TMC5041_MOTORS, TMC5041_A_MAX_REGISTER_2,acceleration_value);
  writeRegister(TMC5041_MOTORS, TMC5041_D_MAX_REGISTER_2,acceleration_value);
  writeRegister(TMC5041_MOTORS, TMC5041_A_1_REGISTER_2,acceleration_value);
  writeRegister(TMC5041_MOTORS, TMC5041_D_1_REGISTER_2,acceleration_value); //the datahseet says it is needed
  writeRegister(TMC5041_MOTORS, TMC5041_V_START_REGISTER_2, 0);
  writeRegister(TMC5041_MOTORS,TMC5041_V_STOP_REGISTER_2,1); //needed acc to the datasheet?
  writeRegister(TMC5041_MOTORS, TMC5041_V_1_REGISTER_2,0);

  //so here is the theoretic trick:
  /*
  homing is just a homing the 4361
   go down a bit until RAMP_STAT velocity_ reached or event_pos_ reached
   if event_stop_r the x_latch can be read 
   
   the following motor is a bit trickier:
   zero all motors before we start - they will be zerored anyway (x_actual)
   closely monitor event_stop_r
   if it is hit stop the motor manually
   - should be fast enough
   - and is an excuse to implement the home moving blocking 
   */

  //TODO obey the timeout!!
  long last_wait_time = millis();

  unsigned char homed = 0; //this is used to track where at homing we are 
  long target = 0;
#ifdef DEBUG_HOMING_STATUS_5041
  unsigned long old_status = -1;
#endif
  //to ensure a easier homing we make both motors use the same refence heigt from here on  so they must be parallel at this point
  if (motor_nr==0) {
    long actual = (long) readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_1);
    writeRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_2,actual);
    writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_2,actual);
  } 
  else {          
    long actual = (long) readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_2);
    writeRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_1,actual);
    writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_1,actual);
  }

  while (homed!=0xff) { //we will never have 255 homing phases - but whith this we not have to think about it 
    status_wait_ping(&last_wait_time, homed);
    unsigned long status=0;
    if (homed==0 || homed==1) {
      unsigned long homing_speed=(unsigned long) homing_fast_speed; 
      if (homed==1) {
        homing_speed = (unsigned long) homing_low_speed;
      }  
      if (motor_nr==0) {
        status = readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_1);
      } 
      else {
        status = readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_2);
      }
#ifdef DEBUG_HOMING_STATUS_5041
      if (status!=old_status) {
        Serial.print("Status1 ");
        Serial.println(status,HEX);
        Serial.print(F("Position "));
        Serial.print((long)readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_1));
        Serial.print(F(", Targe "));
        Serial.println((long)readRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_1));
        Serial.print(F(", Velocity "));
        Serial.println((long)readRegister(TMC5041_MOTORS, TMC5041_V_ACTUAL_REGISTER_1));
        old_status=status;
      }
#endif
      if ((status & (_BV(4) | _BV(0)))==0){ //reference switches not hit
        if (status & (_BV(12) | _BV(10) | _BV(9))) { //not moving or at or beyond target
          target -= 9999999l;
#ifdef DEBUG_HOMING
          Serial.print(F("Homing to "));
          Serial.print(target);
          Serial.print(F(" @ "));
          Serial.println(homing_speed);
          Serial.print(F("Status "));
          Serial.print(status,HEX);
          Serial.print(F(" phase "));
          Serial.println(homed,DEC);
          Serial.print(F("Position "));
          Serial.print(readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_1));
          Serial.print(F(", Velocity "));
          Serial.println((long)readRegister(TMC5041_MOTORS, TMC5041_V_ACTUAL_REGISTER_1));
#endif
          if (motor_nr==0) {
            writeRegister(TMC5041_MOTORS,TMC5041_V_MAX_REGISTER_1, homing_speed);
            writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_1,target);
          } 
          else {            
            writeRegister(TMC5041_MOTORS,TMC5041_V_MAX_REGISTER_2, homing_speed);
            writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_2,target);
          }
          for (char i = 0; i< homing_max_following_motors ;i++) {
            if (followers[i]==0) {
              writeRegister(TMC5041_MOTORS,TMC5041_V_MAX_REGISTER_1, homing_speed);
              writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_1,target);
            } 
            else if (followers[i]==1) {
              writeRegister(TMC5041_MOTORS,TMC5041_V_MAX_REGISTER_2, homing_speed);
              writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_2,target);
            }
          }
        }
      } 
      else{ //reference switches hit
        long go_back_to;
        if (homed==0) {
          long actual;
          if (motor_nr==0) {
            actual = (long) readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_1);
          } 
          else {          
            actual = (long) readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_2);
          }
          go_back_to = actual + homing_retraction;
#ifdef DEBUG_HOMING
          Serial.print(F("home near "));
          Serial.print(actual);
          Serial.print(F(" - going back to "));
          Serial.println(go_back_to);
#endif
        } 
        else {
          if (motor_nr==0) {
            go_back_to = (long) readRegister(TMC5041_MOTORS, TMC5041_X_LATCH_REGISTER_1);
          } 
          else {          
            go_back_to = (long) readRegister(TMC5041_MOTORS, TMC5041_X_LATCH_REGISTER_2);
          }
          if (go_back_to==0) {
            //&we need some kiond of backu if there is something wrong with x_latch 
            if (motor_nr==0) {
              go_back_to = (long) readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_1);
            } 
            else {          
              go_back_to = (long) readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_1);
            }
          }
#ifdef DEBUG_HOMING
          Serial.print(F("homed at "));
          Serial.println(go_back_to);
#endif
        }
        if (motor_nr==0) {
          writeRegister(TMC5041_MOTORS,TMC5041_V_MAX_REGISTER_1,homing_speed);
          writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_1,go_back_to);
        } 
        else {
          writeRegister(TMC5041_MOTORS,TMC5041_V_MAX_REGISTER_2, homing_speed);
          writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_2,go_back_to);
        }        
        for (char i = 0; i< homing_max_following_motors ;i++) {
          if (followers[i]==0) {
            writeRegister(TMC5041_MOTORS,TMC5041_V_MAX_REGISTER_1,homing_speed);
            writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_1,go_back_to);
          } 
          else if (followers[i]==1) {
            writeRegister(TMC5041_MOTORS,TMC5041_V_MAX_REGISTER_2, homing_speed);
            writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_2,go_back_to);
          }
        }
        delay(10ul);
        if (motor_nr==0) {
          status = readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_1);
        } 
        else {
          status = readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_2);
        }
        while (!(status & _BV(9))) { //are we there yet??
          if (motor_nr==0) {
            status = readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_1);
          } 
          else {
            status = readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_2);
          }
        }
        if (homed==0) {
          homed = 1;
        } 
        else {
          if (motor_nr==0) {
            writeRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_1,0);
            writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_1,0);
          } 
          else {
            writeRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_2,0);
            writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_2,0);
          }        
          for (char i = 0; i< homing_max_following_motors ;i++) {
            if (followers[i]==0) {
              writeRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_1,0);
              writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_1,0);
            } 
            else if (followers[i]==1) {
              writeRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_2,0);
              writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_2,0);
            }
          }
          homed=0xff;
        }     
      } 
    } 
  }
  return 0;
}

boolean invertMotorTMC5041(char motor_nr, boolean inverted) {
  unsigned long general_conf= readRegister(TMC5041_MOTORS, TMC5041_GENERAL_CONFIG_REGISTER);
  unsigned long pattern; //used for deletion and setting of the bit
  if (motor_nr==0) {
    pattern = _BV(8);
  } 
  else {
    pattern = _BV(9);
  }
  if (inverted) {
    //set the bit
    general_conf|= pattern;
  } 
  else {
    //clear the bit
    general_conf &= ~pattern;
  }
  writeRegister(TMC5041_MOTORS, TMC5041_GENERAL_CONFIG_REGISTER,general_conf); 
  general_conf= readRegister(TMC5041_MOTORS, TMC5041_GENERAL_CONFIG_REGISTER);
  //finally return if the bit iss set 
  return ((general_conf & pattern)==pattern);
}

void setMotorPositionTMC5041(unsigned char motor_nr, long position) {
#ifdef DEBUG_MOTION_SHORT
  Serial.print('m');
  Serial.print(motor_nr);
  Serial.println(F(":=0"));
#endif
  //we write x_actual an x_target to the same value to be safe 
  if (motor_nr==0) {
    writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_1,position);
    writeRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_1,position);
  } 
  else {
    writeRegister(TMC5041_MOTORS, TMC5041_X_TARGET_REGISTER_2,position);
    writeRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_2,position);
  }
}

unsigned long getClearedEndstopConfigTMC5041(char motor_nr, boolean left) {
  unsigned long endstop_config = endstop_config_shadow[motor_nr];
  //clear everything
  unsigned long clearing_pattern; // - a trick to ensure the use of all 32 bits
  if (left) {
    clearing_pattern = TMC5041_LEFT_ENDSTOP_REGISTER_PATTERN;
  } 
  else {
    clearing_pattern = TMC5041_RIGHT_ENDSTOP_REGISTER_PATTERN;
  }
  clearing_pattern = ~clearing_pattern;
  endstop_config &= clearing_pattern;
  return endstop_config;
}

//void checkTMC5041Motion() {
//  if (tmc5041_read_position_read_status) {
//    tmc5041_read_position_read_status=false;
//    //We are only interested in the first 8 bits 
//    unsigned char events0 = readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_1);
//    unsigned char events1 = readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_2);
//    if ((events0 | events1) & _BV(7)) {
//      //we are only interested in target reached
//      //TODO is that enough?? should not we track each motor??
//      motor_target_reached(nr_of_coordinated_motors);
//    }
//  }  
//}

ISR(PCINT0_vect)
{
  tmc5041_read_position_read_status = true;
}
