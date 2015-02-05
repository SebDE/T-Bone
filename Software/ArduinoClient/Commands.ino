// Die Kommandos die der Beagle senden kann
enum {
  // Kommandos zur Bewegung

  //Komandos zur Konfiguration
  kMotorCurrent = 1,
  kEncoder = 2,
  kEndStops = 3,
  kInvertMotor = 4,
  //intitalize all drivers with default values - TODO or preconfigured??
  kInit=9,
  //Kommandos die Aktionen auslösen
  kMove = 10,
  kMovement = 11, //controls if a new movement is started or a running one is stopped
  kHome=12, //Home one axis
  kSetPos = 13, //set an axis position
  //Kommandos zur Information
  kPos = 30,
  kCommands = 31,
  kStatus = 32,
  //weiteres
  kCurrentReading = 41,
  // direkter SPI-Zugriff
//  kSPI = 44,
  //Sonstiges
  kOK = 0,
  kWait = -1,
  kError =  -9,
  kWarn = -5,
  kInfo = -2,
  kKeepAlive = -128,
};


void attachCommandCallbacks() {
  // Attach callback methods
  messenger.attach(OnUnknownCommand);
  messenger.attach(kInit, onInit);
  messenger.attach(kMotorCurrent, onConfigMotorCurrent);
  messenger.attach(kEncoder, onConfigureEncoder); 
  messenger.attach(kEndStops,onConfigureEndStop);
  messenger.attach(kInvertMotor,onInvertMotor);
  messenger.attach(kMove, onMove);
  messenger.attach(kMovement, onMovement);
  messenger.attach(kSetPos, onSetPosition);
  messenger.attach(kPos, onPosition);
  messenger.attach(kStatus, onStatus);
  messenger.attach(kHome, onHome);
  messenger.attach(kCommands, onCommands);
  //messenger.attach(kCurrentReading, onCurrentReading);
  //messenger.attach(kSPI, onSPI);
}

// ------------------  C A L L B A C K S -----------------------

// Fehlerfunktion wenn ein Kommand nicht bekannt ist
void OnUnknownCommand() {
  messenger.sendCmdStart(kError);
  messenger.sendCmdArg('U');
  messenger.sendCmdArg(messenger.CommandID(),DEC);
  messenger.sendCmdEnd();
}

void onInit() {
  //initialize the 43x
  initialzeTMC4361();
  //start the tmc260 driver
  intializeTMC260();
  //initialize the 5041 chpi
  initialzeTMC5041();
  //we stop the motion anyway
  resetMotion();
  //finally clear the command queue if there migth be some entries left
  while(!moveQueue.isEmpty()) {
    moveQueue.pop();
  }
  //and we are done here
  messenger.sendCmd(kOK,0);
}

//Motor Strom einstellen
void onConfigMotorCurrent() {
  char motor = decodeMotorNumber(true);
  if (motor<0) {
    return;
  }
  int newCurrent = messenger.readIntArg();
  if (newCurrent==0) {
    if (IS_COORDINATED_MOTOR(motor)) {
      messenger.sendCmdStart(kMotorCurrent);
      messenger.sendCmdArg(motor+1);
      messenger.sendCmdArg(motors[motor].tmc260.getCurrent());
      messenger.sendCmdEnd();
    } 
    else {
//      messenger.sendCmd (kError,0); //not implemented yet
      //get back the TMC5041 current 
      messenger.sendCmdStart(kMotorCurrent);
      messenger.sendCmdArg(motor+1);
      messenger.sendCmdArg(getCurrentTMC5041(motor-nr_of_coordinated_motors));
      messenger.sendCmdEnd();
    }
    return;
  }
  if (newCurrent<0) {
    messenger.sendCmd (kError,-1); 
    return;
  }
  char error;
  if (IS_COORDINATED_MOTOR(motor)) {
    error = setCurrentTMC260(motor,newCurrent);
  } 
  else {
    error = setCurrentTMC5041(motor-nr_of_coordinated_motors,newCurrent);
  }
  if (error==NULL) {
    messenger.sendCmd(kOK,0);
  } 
  else {
    messenger.sendCmd(kError,error);
  }
}

void onInvertMotor() {
  char motor = decodeMotorNumber(true);
  if (motor<0) {
    return;
  }
  char invert = messenger.readIntArg();
  if (invert==0) {
    messenger.sendCmdStart(kInvertMotor);
    messenger.sendCmdArg(motor+1);
    if (inverted_motors| _BV(motor)) {
      messenger.sendCmdArg(-1);
    } 
    else {
      messenger.sendCmdArg(1);
    }
    messenger.sendCmdEnd();
    return;
  }
  if (invert<0) {
    if (IS_COORDINATED_MOTOR(motor)) {
      messenger.sendCmd (kError,-100); //not implmented yet
      return;
      /*
      //TODO somehow the endstops are not reacting as expected at least there is a problem with retract after hitting
       inverted_motors |= _BV(motor);
       //invert endstops
       unsigned long endstop_config = readRegister(motor, TMC4361_REFERENCE_CONFIG_REGISTER, 0);
       endstop_config |= _BV(4);
       readRegister(motor, TMC4361_REFERENCE_CONFIG_REGISTER, endstop_config);
       
       messenger.sendCmd(kOK,F("Motor inverted"));
       */
    } 
    else {
      if (invertMotorTMC5041(motor-nr_of_coordinated_motors,true)) {
        messenger.sendCmd(kOK,0);
      } 
      else {
        messenger.sendCmd (kError,-1);
      }
    }
  } 
  else {
    if (IS_COORDINATED_MOTOR(motor)) {

      inverted_motors &= ~(_BV(motor));
      messenger.sendCmd(kOK,0);
    } 
    else {
      if (invertMotorTMC5041(motor-nr_of_coordinated_motors,false)) {
        messenger.sendCmd(kError,-1);
      } 
      else {
        messenger.sendCmd (kOK,0);
      }
    }
  }
}

void onMove() {
#ifdef RX_TX_BLINKY_1
  RXLED1;
#endif

  char motor = decodeMotorNumber(true);
  if (motor<0) {
    return;
  }
  movement move;
  movement followers[MAX_FOLLOWING_MOTORS];
  move.type = move_to;
  move.motor=motor;
  if (readMovementParameters(&move)) {
    //if there was an error return 
    return;
  }
  int following_motors=0;

//  Serial.print("m;");
//  Serial.print(motor,DEC);
//  Serial.print(';');
//  Serial.print(move.target);
//  Serial.print(";");

#ifdef DEBUG_MOTOR_QUEUE
  Serial.print(F("Adding movement for motor "));
  Serial.print(motor,DEC);
  if (move.type==move_to) {
    Serial.print(F(" to "));
  } 
  else {
    Serial.print(F(" via "));
  }
  Serial.print(move.target);
#ifdef DEBUG_MOTION    
  Serial.print(F(", vMax="));
  Serial.print(move.vMax);
  Serial.print(F(", aMax="));
  Serial.print(move.aMax);
  Serial.print(F(": vStart="));
  Serial.print(move.vStart);
  Serial.print(F(": vStop="));
  Serial.print(move.vStop);
#endif    
#endif

  do {
    motor = messenger.readIntArg();
    if (motor!=0) {
      followers[following_motors].type=follow_to;
      followers[following_motors].motor=motor - 1;
      if (readMovementParameters(&followers[following_motors])) {
        //if there was an error return 
        return;
      } 
//      Serial.print(motor,DEC);
//      Serial.print(";");
//      Serial.print(move.target);
//      Serial.print(";");
#ifdef DEBUG_MOTOR_QUEUE
      Serial.print(F(", following motor "));
      Serial.print(motor - 1,DEC);
      if (move.type==follow_to) {
        Serial.print(F(" to "));
      } 
      else {
        Serial.print(F(" via "));
      }
      Serial.print(followers[following_motors].target);
#ifdef DEBUG_MOTION    
      Serial.print(F(", vMax="));
      Serial.print(followers[following_motors].vMax);
      Serial.print(F(", aMax="));
      Serial.print(followers[following_motors].aMax);
      Serial.print(F(": vStart="));
      Serial.print(followers[following_motors].vStart);
      Serial.print(F(": vStop="));
      Serial.print(followers[following_motors].vStop);
#endif    
#endif
      following_motors++;
    }  
  } 
  while (motor!=0);
#ifdef DEBUG_MOTOR_QUEUE
  Serial.println();
#endif
  if (moveQueue.count()+following_motors+1>COMMAND_QUEUE_LENGTH) {
    messenger.sendCmd(kError,-100);
    return;
  }
  moveQueue.push(move);
  for (char i=0; i<following_motors; i++) {
    moveQueue.push(followers[i]);
  }

  messenger.sendCmdStart(kOK);
  messenger.sendCmdArg(moveQueue.count());
  messenger.sendCmdArg(COMMAND_QUEUE_LENGTH);
//  if (in_motion) {
  if (current_motion_state==in_motion) {
    messenger.sendCmdArg(1);
  } 
  else {
    messenger.sendCmdArg(-1);
  }
  messenger.sendCmdEnd();
#ifdef RX_TX_BLINKY_1
  RXLED0;
#endif

} 

char readMovementParameters(movement* move) {
  long newPos = messenger.readLongArg();

  char movementType = (char)messenger.readIntArg();
  boolean isWaypoint;
  if (movementType == 's') {
    //the movement is no waypoint  we do not have to do anything
    isWaypoint = false;
  } 
  else if (movementType== 'w') {
    isWaypoint = true;
  } 
  else {
    /*
    TODO this occassionally does not work - is this an indication for a serial speed problem (noise)
     */
    messenger.sendCmdStart(kError);
    messenger.sendCmdArg(-2);
    messenger.sendCmdArg(movementType,DEC);
    messenger.sendCmdEnd();

    return -3;
    //isWaypoint = false;

  }  
  double vMax = messenger.readFloatArg();
  if (vMax<=0) {
    messenger.sendCmd (kError,-3);
    return -4;
  }
  double aMax = messenger.readFloatArg();
  if (aMax<=0) {
    messenger.sendCmd(kError,-4);
    return -5;
  }
  double vStart = messenger.readFloatArg();
  if ((vStart<0)||(vStart>vMax)) {
    messenger.sendCmd (kError,-5);
    return -6;
  }
  double vStop = messenger.readFloatArg();
  if ((vStop<0)||(vStop>vMax)) {
    messenger.sendCmd (kError,-6);
    return -7;
  }

  move->target = newPos;
  if (isWaypoint) {
    if (move->type==move_to) {
      move->type=move_over;
    } 
    else {
      move->type=follow_over;
    }
  }
  move->vMax=vMax;
  move->aMax=aMax;
  move->vStart=vStart;
  move->vStop=vStop;

  return 0;
}

void onMovement() {
  char movement = messenger.readIntArg();
  if (movement==0) {
    messenger.sendCmdStart(kMovement);
    //just give out the current state of movement
    if (current_motion_state==in_motion) {
      messenger.sendCmdArg(1);
    } 
    else if (current_motion_state==finishing_motion) {
      messenger.sendCmdArg(2);
    }
    else {
      messenger.sendCmdArg(-1);
    }
    messenger.sendCmdEnd();
  } 
  else if (movement<0) {
    //below zero means nostop the motion
    if (!current_motion_state==in_motion) {
      messenger.sendCmd(kError,-1);
    } 
    else {
      //TODO should we handle double finishing ??
#ifdef DEBUG_MOTION_STATUS
      Serial.println(F("motion will finish"));
#endif
      messenger.sendCmd(kOK,0);
      finishMotion();
    }
  } 
  else {
    char initial_command_buffer_depth = messenger.readIntArg(); //defaults to zero and is optionnal
    //a new movement is started
    if (current_motion_state!=no_motion) {
      messenger.sendCmd(kError,-1);
    } 
    else {
#ifdef DEBUG_MOTION_STATUS
      Serial.println(F("motion will start"));
#endif
      messenger.sendCmd(kOK,0);
      startMotion(initial_command_buffer_depth);
    }
  }
}

void onSetPosition() {
  char motor = decodeMotorNumber(true);
  if (motor<0) {
    return;
  }
  long newPos = messenger.readLongArg();
  if (newPos<0) {
    messenger.sendCmd (kError,-1);
    return;
  }
#ifdef DEBUG_SET_POS
  Serial.print(F("Enqueing setting motor "));
  Serial.print(motor,DEC);
  Serial.print(F(" to position "));
  Serial.println(newPos,DEC);
#endif  
  //configure a movement
  movement move;
  move.type = set_position;
  move.motor=motor;
  move.target=newPos;
  moveQueue.push(move);

  messenger.sendCmdStart(kOK);
  messenger.sendCmdArg(moveQueue.count());
  messenger.sendCmdArg(COMMAND_QUEUE_LENGTH);
}

void onPosition() {
  char motor = decodeMotorNumber(true);
  if (motor<0) {
    return;
  }
  long position;
  if (IS_COORDINATED_MOTOR(motor)) {

    position = (long)readRegister(motor,TMC4361_X_ACTUAL_REGISTER);
  } 
  else {
    if (motor == nr_of_coordinated_motors) {
      position = (long) readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_1);
    } 
    else {
      position = (long) readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_2);
    }
  }
  messenger.sendCmdStart(kPos);
  messenger.sendCmdArg(position);
  messenger.sendCmdEnd();
}

//void onStatus() {
//  char motor = decodeMotorNumber(true);
//  if (motor<0) {
//    return;
//  }
//  if (IS_COORDINATED_MOTOR(motor)) {
//    unsigned long status = readRegister(motor, TMC4361_STATUS_REGISTER);
//    long position = (long)readRegister(motor,TMC4361_X_ACTUAL_REGISTER);
//    long encoder_pos = (long)readRegister(motor,TMC4361_ENCODER_POSITION_REGISTER);
//    messenger.sendCmdStart(kStatus);
//    messenger.sendCmdArg(position);
//    messenger.sendCmdArg(encoder_pos);
//    if (status & (_BV(7) | _BV(9))) {
//      messenger.sendCmdArg(1,DEC);
//    } 
//    else {
//      messenger.sendCmdArg(-1,DEC);
//    }
//    if (status & (_BV(8) | _BV(10))) {
//      messenger.sendCmdArg(1,DEC);
//    } 
//    else {
//      messenger.sendCmdArg(-1,DEC);
//    }
//    messenger.sendCmdArg(encoder_pos);
//    messenger.sendCmdEnd();
//  } 
//  else {
//    long position;
//    if (motor == nr_of_coordinated_motors) {
//      position = (long) readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_1);
//    } 
//    else {
//      position = (long) readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_2);
//    }
//    messenger.sendCmdStart(kStatus);
//    messenger.sendCmdArg(position);
//    messenger.sendCmdEnd();
//  }
//}  
//
void onStatus() {
  unsigned long status;
  long position;
  long encoder_pos;
  char stopl, stopr;
  char motor = decodeMotorNumber(true);
  
  switch (motor) {
    case 0:
    case 1:
    case 2:
      // TMC4361 axis 1 - 3
      status = readRegister(motor, TMC4361_STATUS_REGISTER);
      position = (long)readRegister(motor,TMC4361_X_ACTUAL_REGISTER);
      encoder_pos = (long)readRegister(motor,TMC4361_ENCODER_POSITION_REGISTER);
      break;
      
    case 3:
      // TMC5041 axis 1
      status = readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_1);
      position = (long) readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_1);
      encoder_pos = 0; // we don't have an encoder
      break;

    case 4:
      // TMC5041 axis 2
      status = readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_2);
      position = (long) readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_2);
      encoder_pos = 0; // we don't have an encoder
      break;

    default:
      return;  
  }

  switch (motor) {
    case 0:
    case 1:
    case 2:
      // TMC4361 axis
      if (status & (_BV(7) | _BV(9))) {
        stopl = 1;
      } else {
        stopl = -1;
      }
      if (status & (_BV(8) | _BV(10))) {
        stopr = 1;
      } else {
        stopr = -1;
      }
      break;
      
    case 3:
    case 4:
      // TMC5041 axis
      if (status & _BV(0)) {
        stopl = -1;
      } else {
        stopl = 1;
      }
      if (status & _BV(1)) {
        stopr = -1;
      } else {
        stopr = 1;
      }
      break;

    default:
      return;  
  }

  messenger.sendCmdStart(kStatus);
  messenger.sendCmdArg(status);
  messenger.sendCmdArg(position);
  messenger.sendCmdArg((int)stopl);
  messenger.sendCmdArg((int)stopr);
  messenger.sendCmdArg(encoder_pos);
  messenger.sendCmdEnd();
  
/*
  if (motor<0) {
    return;
  }
  if (IS_COORDINATED_MOTOR(motor)) {
    status = readRegister(motor, TMC4361_STATUS_REGISTER);
    position = (long)readRegister(motor,TMC4361_X_ACTUAL_REGISTER);
    encoder_pos = (long)readRegister(motor,TMC4361_ENCODER_POSITION_REGISTER);
    messenger.sendCmdStart(kStatus);
    messenger.sendCmdArg(status);
    messenger.sendCmdArg(position);
    if (status & (_BV(7) | _BV(9))) {
      messenger.sendCmdArg(1,DEC);
    } 
    else {
      messenger.sendCmdArg(-1,DEC);
    }
    if (status & (_BV(8) | _BV(10))) {
      messenger.sendCmdArg(1,DEC);
    } 
    else {
      messenger.sendCmdArg(-1,DEC);
    }
    messenger.sendCmdArg(encoder_pos);
    messenger.sendCmdEnd();
  } 
  else {
    if (motor == nr_of_coordinated_motors) {
      status = readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_1);
      position = (long) readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_1);
    } 
    else {
      status = readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_1);
      position = (long) readRegister(TMC5041_MOTORS, TMC5041_X_ACTUAL_REGISTER_2);
    }
    messenger.sendCmdStart(kStatus);
    messenger.sendCmdArg(status);
    messenger.sendCmdArg(position);
    if (status & _BV(0)) {
      messenger.sendCmdArg(1,DEC);
    } 
    else {
      messenger.sendCmdArg(-1,DEC);
    }
    if (status & _BV(1)) {
      messenger.sendCmdArg(1,DEC);
    } 
    else {
      messenger.sendCmdArg(-1,DEC);
    }
    messenger.sendCmdEnd();
  }
*/
}  

void onConfigureEncoder() {
  char motor = decodeMotorNumber(true);
  if (motor<0) {
    return;
  }
  if (IS_COORDINATED_MOTOR(motor)) {
    int enableEncoder = messenger.readIntArg();
    if (enableEncoder==0) {
      messenger.sendCmd(kError, 0);
      //TODO do we need an method to read out the encoder config??
    } 
    else if (enableEncoder<1) {
      //disable encoder and closed loop
      configureEncoderTMC4361(motor,0,256, 0, false, false);
    } 
    else {
      //configure encoder
      unsigned int steps_per_revolution = messenger.readIntArg();
      if (steps_per_revolution<1) {
        messenger.sendCmd(kError,-1);
      }
      unsigned int microsteps_per_step = messenger.readIntArg();
      if (microsteps_per_step<1) {
        messenger.sendCmd(kError,-1);
      }
      unsigned int encoder_increments_per_revolution = messenger.readIntArg();
      if (encoder_increments_per_revolution<1) {
        messenger.sendCmd(kError,-2);
      }
      int encoder_differential = messenger.readIntArg();
      if (encoder_differential==0) {
        messenger.sendCmd(kError,-3);
      }
      int encoder_inverted = messenger.readIntArg();
      if (encoder_inverted==0) {
        messenger.sendCmd(kError,-4);
      }
      configureEncoderTMC4361(motor,steps_per_revolution, microsteps_per_step, encoder_increments_per_revolution, encoder_inverted>0,encoder_differential>0);
    }
    messenger.sendCmd(kOK, 0);
  } 
  else {
    messenger.sendCmd(kError, -1);
  }
}



void onConfigureEndStop() {
  char motor = decodeMotorNumber(true);
  if (motor<0) {
    return;
  }
  int position = messenger.readIntArg();
  if (position==0) {
    messenger.sendCmd(kError,-1);
  }
  int type = messenger.readIntArg();
  if (type!=0 && type!=1) {
    messenger.sendCmd(kError,-1);
    return;
  }
  char error;
  switch(type) {
  case 0: //virtual endstops
    {
      long virtual_pos = messenger.readLongArg();
      if (IS_COORDINATED_MOTOR(motor)) {
        error = configureVirtualEndstopTMC4361(motor, position<0, virtual_pos);
      } 
      else {
        error = configureVirtualEndstopTMC5041(motor, position<0, virtual_pos);
      }
    }
    break;
  case 1: //real endstop
    {
      int polarity = messenger.readIntArg();
      if (polarity==0) {
        messenger.sendCmd(kError,-1);
        return;
      }
      if (IS_COORDINATED_MOTOR(motor)) {
        error= configureEndstopTMC4361(motor, position<0, polarity>0);
      } 
      else {
        error= configureEndstopTMC5041(motor-nr_of_coordinated_motors, position<0, true, polarity>0);
      }     
    }
    break;
  }
  if (error!=NULL) {
    messenger.sendCmd(kError,error);
  } 
  else {
    messenger.sendCmd(kOK,0);
  }
}

void onHome() {
  //TODO wew will need a timeout afte which we have to stop homing 
  char motor = decodeMotorNumber(true);
  if (motor<0) {
    return;
  }
  long timeout = messenger.readLongArg();
  if (timeout<=0) {
    timeout=0;
  }
  double homeFastSpeed = messenger.readFloatArg();
  if (homeFastSpeed<=0) {
    messenger.sendCmd (kError,-1);
    return;
  }
  double homeSlowSpeed = messenger.readFloatArg();
  if (homeSlowSpeed<=0) {
    messenger.sendCmd (kError,-2);
    return;
  }
  long homeRetract = messenger.readLongArg();
  if (homeRetract<=0) {
    messenger.sendCmd(kError,-3);
    return;
  }
  double aMax = messenger.readFloatArg();
  if (aMax<=0) {
    messenger.sendCmd(kError,-4);
    return;
  }
  char error;
  if (motor<nr_of_coordinated_motors) {

    unsigned long home_right_position = messenger.readLongArg();

    error =  homeMotorTMC4361(
    motor, timeout,
    homeFastSpeed, homeSlowSpeed, homeRetract, aMax, home_right_position);
  } 
  else {
    char following_motors[homing_max_following_motors]; //we can only home follow controlled motors
    for (char i = 0; i<homing_max_following_motors ;i++) {
      following_motors[i] = decodeMotorNumber(false);
      if (following_motors[i]==-1) {
        break;
      } 
      else {
        following_motors[i] -= nr_of_coordinated_motors;
      }
    }
    error =  homeMotorTMC5041(
    motor-nr_of_coordinated_motors,timeout,
    homeFastSpeed, homeSlowSpeed,homeRetract,aMax,following_motors);
  }
  if (error==NULL) {
    messenger.sendCmdStart(kOK);
    messenger.sendCmdArg(motor,DEC);
    messenger.sendCmdEnd();
  } 
  else {
    messenger.sendCmd(kError,error);
    messenger.sendCmdStart(kError);
    messenger.sendCmdArg(error);
    messenger.sendCmdArg(motor,DEC);
    messenger.sendCmdEnd();
  }
}

void onCommands() {
  messenger.sendCmdStart(kCommands);
  messenger.sendCmdArg(moveQueue.count());
  messenger.sendCmdArg(COMMAND_QUEUE_LENGTH);
  messenger.sendCmdEnd();
#ifdef DEBUG_STATUS
  int ram = freeRam();
  Serial.print(F("Queue: "));
  Serial.print(moveQueue.count());
  Serial.print(F(" of "));
  Serial.print(COMMAND_QUEUE_LENGTH);
  Serial.print(F("\tRAM:  "));
  Serial.print(ram);
  if (current_motion_state==no_motion) {
    Serial.println(F("\t - not moving"));
  } 
  else if (current_motion_state==in_motion) {
    Serial.println(F("\t - in motion"));
  } 
  else if (current_motion_state==finishing_motion) {
    Serial.println(F("\t - finishing motion"));
  } 
  else {
    Serial.println(F("Unkmown motion"));
  }  
  Serial.println();
#endif
}

void onCurrentReading() {
  //todo this is a hack because the current reading si onyl avail on arduino
  char number = messenger.readIntArg();
  if (number!=0) {
    number = 1;
  }
  unsigned int value = 0;
  for (char i=0; i<4; i++) {
    value += analogRead(4+number);
  }
  value >> 2;
  messenger.sendCmdStart(kCurrentReading);
  messenger.sendCmdArg(number,DEC);
  messenger.sendCmdArg(value);
  messenger.sendCmdEnd();
}

/*
void onSPI() {
  char axis_number = decodeMotorNumber(true);
  char reg = messenger.readIntArg();
  long data_o = messenger.readLongArg();
  byte spi_status;
  long data_i;
  
  switch (axis_number) {
    case 0:
      digitalWriteFast(CS_4361_1_PIN,HIGH);
      break;  
    case 1:
      digitalWriteFast(CS_4361_2_PIN,HIGH);
      break;  
    case 2:
      digitalWriteFast(CS_4361_3_PIN,HIGH);
      break;  
    case 3:
    case 4:
      digitalWriteFast(CS_5041_PIN,HIGH);
      break;  
  }

  spi_status = SPI.transfer(reg);
  data_i = SPI.transfer((data_o >> 24) & 0xff);
  data_i <<= 8;
  data_i |= SPI.transfer((data_o >> 16) & 0xff);
  data_i <<= 8;
  data_i |= SPI.transfer((data_o >>  8) & 0xff);
  data_i <<= 8;
  data_i |= SPI.transfer((data_o) & 0xff);
  
  digitalWriteFast(CS_4361_1_PIN,LOW);
  digitalWriteFast(CS_4361_2_PIN,LOW);
  digitalWriteFast(CS_4361_3_PIN,LOW);
  digitalWriteFast(CS_5041_PIN,LOW);

  messenger.sendCmdStart(kSPI);
  messenger.sendCmdArg(spi_status,HEX);
  messenger.sendCmdArg(data_i,HEX);
  messenger.sendCmdEnd();
}
*/

void watchDogPing() {

  messenger.sendCmdStart(kKeepAlive);
  messenger.sendCmdArg(moveQueue.count());
  messenger.sendCmdArg(COMMAND_QUEUE_LENGTH);
  messenger.sendCmdEnd();
#ifdef DEBUG_STATUS
  int ram = freeRam();
  Serial.print(F("Queue: "));
  Serial.print(moveQueue.count());
  Serial.print(F(" of "));
  Serial.print(COMMAND_QUEUE_LENGTH);
  Serial.print(F("\tRAM:  "));
  Serial.print(ram);
  if (current_motion_state==no_motion) {
    Serial.println(F("\t - not moving"));
  } 
  else if (current_motion_state==in_motion) {
    Serial.println(F("\t - in motion"));
  } 
  else if (current_motion_state==finishing_motion) {
    Serial.println(F("\t - finishing motion"));
  } 
  else {
    Serial.println(F("Unkmown motion"));
  }  
  Serial.println();
#endif
#ifdef DEBUG_STATUS_SHORT
  Serial.print('Q');
  Serial.print(moveQueue.count());

  Serial.print('/');
  Serial.print(min_buffer_depth);

  if (next_move_prepared) {
    Serial.print('+');
  }
  if (move_executing) {
    Serial.print('~');
  }

  if (current_motion_state==no_motion) {
    Serial.println('s');
  } 
  else if (current_motion_state==in_motion) {
    Serial.println('m');
  } 
  else if (current_motion_state==finishing_motion) {
    Serial.println('f');
  } 
  else {
    Serial.println(F("Unkmown motion"));
  }  
#endif
#ifdef DEBUG_TMC5041_STATUS
  Serial.print(F("#1: "));
  Serial.print(readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_1),HEX);
  Serial.print(F("\t#2: "));
  Serial.println(readRegister(TMC5041_MOTORS, TMC5041_RAMP_STATUS_REGISTER_2),HEX);
  Serial.println();
#endif
#ifdef DEBUG_TMC4361_STATUS
  for (char i=0; i<nr_of_coordinated_motors;i++) {
    Serial.print(readRegister(i, TMC4361_STATUS_REGISTER),HEX);
    Serial.print(' ');
    Serial.print(readRegister(i, TMC4361_START_CONFIG_REGISTER),HEX);
    Serial.print(' ');
    Serial.print((long)readRegister(i, TMC4361_X_ACTUAL_REGISTER),DEC);
    Serial.print(' ');
    Serial.print((long)readRegister(i, TMC4361_X_TARGET_REGISTER),DEC);
    Serial.print(' ');
    Serial.print((long)readRegister(i, TMC4361_V_ACTUAL_REGISTER),DEC);
    Serial.print(' ');
    Serial.println((long)readRegister(i, TMC4361_V_MAX_REGISTER),DEC);
  }
  Serial.println();
#endif
}

void watchDogStart() {
  messenger.sendCmd(kOK,0);
  watchDogPing();
}

char decodeMotorNumber(const boolean complaint) {
  char motor = messenger.readIntArg();
  if (motor<1) {
    if (complaint) {
      messenger.sendCmdStart(kError);
      messenger.sendCmdArg(motor,DEC);
      messenger.sendCmdArg(1,DEC);
      messenger.sendCmdArg(nr_of_coordinated_motors,DEC);
      messenger.sendCmdEnd();
    }
    return -1;
  } 
  else if (motor>nr_of_motors) {
    if (complaint) {
      messenger.sendCmdStart(kError);
      messenger.sendCmdArg(motor,DEC);
      messenger.sendCmdArg(1,DEC);
      messenger.sendCmdArg(nr_of_motors,DEC);
      messenger.sendCmdEnd();
    }
    return -1;
  } 
  else {
    return motor - 1;
  }
}

void status_wait_ping(long* last_wait_time, int status) {
  //do we have to ping??
  if (millis()-(*last_wait_time)>1000) {
    messenger.sendCmd(kWait,status);
    *last_wait_time=millis();
  }
}

// see http://rollerprojects.com/2011/05/23/determining-sram-usage-on-arduino/ 
int freeRam() {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}




