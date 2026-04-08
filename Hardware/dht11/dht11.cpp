//
// dht11.c  香橙派5B 最终可用版
//
#include <wiringPi.h>
#include <stdio.h>
#include <stdlib.h>

typedef unsigned char uint8;
typedef unsigned int  uint16;
typedef unsigned long uint32;

#define HIGH_TIME 32

// ===================== 正确引脚 =====================
int pinNumber = 2;  // 物理7脚 → wPi编号 = 2
// ====================================================

uint32 databuf;

uint8 readSensorData(void)
{
    uint8 crc = 0;
    uint8 i;

    pinMode(pinNumber, OUTPUT);
    digitalWrite(pinNumber, 1);
    delayMicroseconds(4);
    digitalWrite(pinNumber, 0);
    delay(20);
    digitalWrite(pinNumber, 1);
    delayMicroseconds(40);
    pinMode(pinNumber, INPUT);
    pullUpDnControl(pinNumber, PUD_UP);

    if(digitalRead(pinNumber) == 0)
    {
        while(!digitalRead(pinNumber));
        delayMicroseconds(80);

        for(i=0; i<32; i++)
        {
            while(digitalRead(pinNumber));
            while(!digitalRead(pinNumber));
            delayMicroseconds(HIGH_TIME);
            databuf *= 2;
            if(digitalRead(pinNumber) == 1)
                databuf++;
        }

        for(i=0; i<8; i++)
        {
            while(digitalRead(pinNumber));
            while(!digitalRead(pinNumber));
            delayMicroseconds(HIGH_TIME);
            crc *= 2;
            if(digitalRead(pinNumber) == 1)
                crc++;
        }

        uint8 check = ((databuf>>24)&0xFF) + ((databuf>>16)&0xFF) + ((databuf>>8)&0xFF) + (databuf&0xFF);
        if(check == crc)
            return 1;
    }
    return 0;
}

int main (void)
{
    if(wiringPiSetup() == -1)
        return 1;

    pinMode(pinNumber, OUTPUT);
    digitalWrite(pinNumber, 1);
    while(1)
    {
        if(!readSensorData())
        {
            printf("{\"RH\":\"%d.%d\", \"TMP\":\"%d.%d\"}",
                (databuf>>24)&0xFF, (databuf>>16)&0xFF,
                (databuf>>8)&0xFF, databuf&0xFF);
        }
        delay(2000);
    }
  

    else
    {
        printf("");
    }

    return 0;
}
