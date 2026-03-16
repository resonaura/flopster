//this Arduino sketch has been used to record sample sequences for Flopster

#include <TimerOne.h>

enum {
  MODE_MOTOR = 0,
  MODE_STEPS,
  MODE_SEEK,
  MODE_BUZZ,
  MODE_ALL
};

unsigned char mode = MODE_ALL;



const int pinStep = 2;
const int pinDir = 3;
const int pinMotor = 4;

volatile unsigned char move_dir = 0;
volatile unsigned char loops = 0;
volatile unsigned char steps = 0;
volatile unsigned char note = 0;
volatile unsigned char loops_max=0;
volatile unsigned char buzzing=0;

volatile unsigned int loop_delay = 0;

volatile unsigned char note_min;
volatile unsigned char note_max;

const unsigned int note_table[12 * 3] = {
  65.41 * 1,
  69.30 * 1,
  73.42 * 1,
  77.78 * 1,
  82.41 * 1,
  87.31 * 1,
  92.50 * 1,
  98.00 * 1,
  103.83 * 1,
  110.00 * 1,
  116.54 * 1,
  123.47 * 1,

  65.41 * 2,
  69.30 * 2,
  73.42 * 2,
  77.78 * 2,
  82.41 * 2,
  87.31 * 2,
  92.50 * 2,
  98.00 * 2,
  103.83 * 2,
  110.00 * 2,
  116.54 * 2,
  123.47 * 2,

  65.41 * 4,
  69.30 * 4,
  73.42 * 4,
  77.78 * 4,
  82.41 * 4,
  87.31 * 4,
  92.50 * 4,
  98.00 * 4,
  103.83 * 4,
  110.00 * 4,
  116.54 * 4,
  123.47 * 4,
  //rest of the table is too fast, floppies usually jam at previous note or two
/*
  65.41 * 8,
  69.30 * 8,
  73.42 * 8,
  77.78 * 8,
  82.41 * 8,
  87.31 * 8,
  92.50 * 8,
  98.00 * 8,
  103.83 * 8,
  110.00 * 8,
  116.54 * 8,
  123.47 * 8*/
};



void park_head(void)
{
  unsigned char i,j;

  Serial.println("parking head");
  
  move_dir = HIGH; //to the outer edge

  for (i = 0; i < 80; ++i)
  {
    digitalWrite(pinDir, move_dir);
    digitalWrite(pinStep, LOW);
    delayMicroseconds(100);
    digitalWrite(pinStep, HIGH);
    for(j=0;j<4;++j) delayMicroseconds(1600);
  }

  digitalWrite(pinStep, LOW);

  Serial.println("parking done");

  move_dir ^= HIGH;
}



void head_to_middle(void)
{
  unsigned char i,j;
  
  Serial.println("moving head to the middle");
  
  for (i = 0; i < 40; ++i)
  {
    digitalWrite(pinDir, move_dir);
    digitalWrite(pinStep, LOW);
    delayMicroseconds(100);
    digitalWrite(pinStep, HIGH);
    for(j=0;j<4;++j) delayMicroseconds(1600);
  }

  digitalWrite(pinStep, LOW);

  Serial.println("head is in the middle");
}



void set_seek_or_buzz(void)
{
  note = note_min;
  steps = 0;
  loops = 0;

  if(!buzzing)
  {
    loops_max=4;
  }
  else
  {
    loops_max=1;
  }
}



void setup() {

  unsigned char i,j;

  note_min = 0;
  note_max = sizeof(note_table) / sizeof(unsigned int);

  Serial.begin(9600);

  digitalWrite(pinDir, HIGH); 	//HIGH to the outer edge of disk, LOW to the center of disk
  digitalWrite(pinStep, HIGH);
  digitalWrite(pinMotor, HIGH); //HIGH motor is off, LOW motor is on

  pinMode(pinStep, OUTPUT);
  pinMode(pinDir, OUTPUT);
  pinMode(pinMotor, OUTPUT);

  delay(100);

  park_head();

  delay(1000);

  if (mode==MODE_ALL||mode == MODE_MOTOR)
  {
    Serial.println("running motor for 10 seconds");
    
    digitalWrite(pinMotor, LOW);  //turning motor on
    
    for (i = 0; i < 10; ++i)
    {
      delay(1000);
      Serial.print(".");
    }
    
    Serial.println("");
	
    digitalWrite(pinMotor, HIGH); //turning motor off
	
    Serial.println("stopping and wait a bit");
    delay(3000);
    
    if(mode!=MODE_ALL) return;
  }

  if (mode==MODE_ALL||mode == MODE_STEPS)
  {
    Serial.println("performing 80 steps");
    
    //slowly moving the head into each position

    for (i = 0; i < 80; ++i)
    {
      digitalWrite(pinDir, move_dir);
      digitalWrite(pinStep, LOW);
      delayMicroseconds(100);
      digitalWrite(pinStep, HIGH);
      delay(500);
      Serial.print(".");
    }

    Serial.println("");

    digitalWrite(pinStep, LOW);

    Serial.println("steps done");
    Serial.println("");
    
    delay(3000);

    if(mode!=MODE_ALL) return;
  }

  park_head();

  if (mode==MODE_ALL||mode == MODE_BUZZ)
  {
    head_to_middle();

    buzzing=1;
  }

  if(mode==MODE_SEEK) buzzing=0;
 
  delay(1000);

  set_seek_or_buzz();

  Timer1.attachInterrupt(Timer1_action);
  Timer1.initialize((long)(1000000.0 / note_table[note]));
}



void Timer1_action()
{
  if(loop_delay)
  {
    --loop_delay;

    return;
  }
  
  if (!loops)
  {
    if (note < note_max)
    {
      Timer1.setPeriod((long)(1000000.0 / note_table[note]));
      steps = 0;
      loops = loops_max;
      ++note;
    }
    else
    {
    if(mode==MODE_ALL)
    {
      mode=255;
      buzzing=0;
      park_head();
      set_seek_or_buzz();
      loop_delay = 1*100;
    }
    }

    return;
  }

  digitalWrite(pinDir, move_dir);
  digitalWrite(pinStep, LOW);
  delayMicroseconds(100);
  digitalWrite(pinStep, HIGH);

  ++steps;

  if (steps >= 80)
  {
    steps = 0;

    if(!buzzing) move_dir ^= HIGH;

    if (loops) --loops;

    if (!loops)
    {
      loop_delay = 1*100;
      Timer1.setPeriod(1000000 / 100);
      digitalWrite(pinStep, LOW);
    }
  }

  if(buzzing) move_dir ^= HIGH;
}



void loop() {


}
