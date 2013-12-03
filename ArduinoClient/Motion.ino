volatile boolean next_move_prepared = false;
volatile boolean prepare_shaddow_registers = false;
volatile unsigned int motor_status;
volatile unsigned int target_motor_status;

void startMotion() {
  in_motion = true;
  next_move_prepared=false; //TODO in theory this is not needed  
  prepare_shaddow_registers = false;
  //TODO initialize drivers??
  for (char i; i<nr_of_motors;i++) {
    write43x(motors[i].cs_pin, START_OUT_ADD_REGISTER, 16000000ul);
  }
}

void stopMotion() {
  in_motion = false;
}

void checkMotion() {
  if (in_motion && !next_move_prepared) {

    if (moveQueue.count()>0) {
      byte moving_motors=0;
      //analysze the movement (nad take a look at the next
      movement move = moveQueue.pop();
      movement followers[MAX_FOLLOWING_MOTORS];
      //check out which momtors are geared with this

#ifdef DEBUG_MOTION
      Serial.print(F("Moving motor "));
      Serial.print(move.motor,DEC);
      Serial.print(F(" to "));
      Serial.print(move.data.move.target);
      Serial.print(F(" at "));
      Serial.println(move.data.move.vmax);
#endif
      moveMotor(move.motor, move.data.move.target,move.data.move.vmax, 1, prepare_shaddow_registers);
      moving_motors |= _BV(move.motor);

      movement follower;
      do {
        follower = moveQueue.peek();
        if (follower.type==followmotor) {  
          moveQueue.pop();
#ifdef DEBUG_MOTION
          Serial.print(F(", following motor "));
          Serial.print(follower.motor,DEC);
          Serial.print(F(" by "));
          Serial.println(follower.data.follow.factor,DEC);
#endif
          moveMotor(follower.motor, follower.data.follow.target,move.data.move.vmax, follower.data.follow.factor, prepare_shaddow_registers);
          moving_motors |= _BV(follower.motor);
        }
      } 
      while (follower.type == followmotor);

      //in the end all moviong motorts must have apssed pos_comp
      target_motor_status = moving_motors;

      for (char i; i<nr_of_motors;i++) {
        //configure all non moving motors to stop
        if (moving_motors && _BV(i) == 0) {
          moveMotor(i, 0,0, 0, prepare_shaddow_registers);
        }

        //give all motors a nice start signal
        if (!prepare_shaddow_registers) {
          write43x(motors[i].cs_pin, START_CONFIG_REGISTER, 0
            | _BV(0) //xtarget requires start
          | _BV(1) //vmax requires start
          | _BV(5) //external start is an start
          | _BV(10)//immediate start         
          );   
        } 
        else {
          write43x(motors[i].cs_pin, START_CONFIG_REGISTER, 0
            | _BV(0) //x_target requires start
          | _BV(4)  //use shaddow motion profiles
          | _BV(5) //external start is an start
          );   
        }
      }

      //for the first move we need to configure everything a bit 
      if (!prepare_shaddow_registers) {
        signal_start();
        //and we need to prepare the next move for the shadow registers
        prepare_shaddow_registers = true;
        next_move_prepared = false;
      } 
      else {
        //ok normally we can relax until the enxt start event occured
        next_move_prepared = true;
      }
#ifdef DEBUG_MOTION
      Serial.println();
#endif
    } 
    else {
      //we are finished here
      stopMotion();
    }
  }
}

void motor_1_target_reached() {
  motor_target_reached(0);
}

void motor_2_target_reached() {
  motor_target_reached(1);
}

void motor_target_reached(char motor_nr) {
  if (in_motion) {
#ifdef DEBUG_MOTION_TRACE
    Serial.print(F("Motor "));
    Serial.print(motor_nr,DEC);
    Serial.print(F("reached target! At"));
    Serial.print(motor_status,BIN);
    Serial.print(F(" of "));
    Serial.println(target_motor_status,BIN);
#endif
    //clear the event register
    read43x(motors[motor_nr].cs_pin,EVENTS_REGISTER,0);
    //and write down which motor touched the target
    motor_status |= _BV(motor_nr);  
    if (motor_status == target_motor_status) {
      //TODO we need some kind of 'At least here'??
#ifdef DEBUG_MOTION_TRACE
      Serial.println(F("all motors reached target!"));
#endif
      next_move_prepared=false;
    }
  }
}













































