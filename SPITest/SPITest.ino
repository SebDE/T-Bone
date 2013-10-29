#include <SPI.h>
#include <TMC26XGenerator.h>
#include <Metro.h>

#define DEBUG

//config
unsigned char steps_per_revolution = 200;
unsigned int current_in_ma = 500;
long vmax = 10000;
long amax = vmax/100;
long dmax = amax;

//register
#define GENERAL_CONFIG_REGISTER 0x0
#define START_CONFIG_REGISTER 0x2
#define SPIOUT_CONF_REGISTER 0x04
#define STEP_CONF_REGISTER 0x0A
#define STATUS_REGISTER 0x0e
#define RAMP_MODE_REGISTER 0x20
#define V_MAX_REGISTER 0x24
#define A_MAX_REGISTER 0x28
#define D_MAX_REGISTER 0x29
#define CLK_FREQ_REGISTER 0x31
#define X_TARGET_REGISTER 0x37
#define COVER_LOW_REGISTER 0x6c
#define COVER_HIGH_REGISTER 0x6d

//values
#define TMC_26X_CONFIG 0x8440000a //SPI-Out: block/low/high_time=8/4/4 Takte; CoverLength=autom; TMC26x
#define TMC260_SENSE_RESISTOR_IN_MO 150
#define CLOCK_FREQUENCY 16000000ul

//we have a TMC260 at the end so we configure a configurer
TMC26XGenerator tmc260 = TMC26XGenerator(current_in_ma,TMC260_SENSE_RESISTOR_IN_MO);

//a metro to control the movement
Metro moveMetro = Metro(5000ul);
Metro checkMetro = Metro(1000ul);

int cs_squirrel = 7;
int reset_squirrel = 2;

void setup() {
  //initialize the serial port for debugging
  Serial.begin(9600);

  //reset the quirrel
  pinMode(reset_squirrel,OUTPUT);
  digitalWrite(reset_squirrel, LOW);
  delay(1);
  digitalWrite(reset_squirrel, HIGH);
  //initialize SPI
  SPI.begin();
  pinMode(cs_squirrel,OUTPUT);
  digitalWrite(cs_squirrel,HIGH);
  //configure the TMC26x
  write43x(GENERAL_CONFIG_REGISTER,_BV(9) | _BV(1) | _BV(2)); //we use xtarget
  write43x(CLK_FREQ_REGISTER,CLOCK_FREQUENCY);
  write43x(START_CONFIG_REGISTER,_BV(10)); //start automatically
  write43x(RAMP_MODE_REGISTER,_BV(2) | 1); //we want to go to positions in nice S-Ramps


  tmc260.setMicrosteps(256);
  write43x(SPIOUT_CONF_REGISTER,TMC_26X_CONFIG);
  set260Register(tmc260.getDriverControlRegisterValue());
  set260Register(tmc260.getChopperConfigRegisterValue());
  set260Register(tmc260.getStallGuard2RegisterValue());
  set260Register(tmc260.getDriverConfigurationRegisterValue() | 0x80);

  //configure the motor type
  unsigned long motorconfig = 0x00; //we want closed loop operation
  motorconfig |= steps_per_revolution<<4;
  write43x(STEP_CONF_REGISTER,motorconfig);

}

unsigned long tmc43xx_write;
unsigned long tmc43xx_read;

unsigned long target=0;

void loop() {
  if (target==0 | moveMetro.check()) {
    target=random(100000ul);
    unsigned long this_v = vmax+random(100)*vmax;
  write43x(V_MAX_REGISTER,this_v << 8); //set the velocity - TODO recalculate float numbers
  write43x(A_MAX_REGISTER,amax); //set maximum acceleration
  write43x(D_MAX_REGISTER,dmax); //set maximum deceleration
    write43x(X_TARGET_REGISTER,target);
    Serial.print("Move to ");
    Serial.println(target);
    Serial.println();
  }
  if (checkMetro.check()) {
    // put your main code here, to run repeatedly: 
    read43x(0x21,0);
    Serial.print("x actual:");
    read43x(0x21,0);
    Serial.println();
  }
}

