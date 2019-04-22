/*
* HQCounter.c
*
* Created: 07.12.2014 13:49:50
*  Author: Andre Tippel
*/

#define F_CPU 1000000UL      // 8 MHz (fuer delay.h)

#define uchar unsigned char
#define uint unsigned int
#define idata

#define SKIP_ROM        0xCC
#define READ_SCRATCHPAD 0xBE
#define CONVERT_TEMP    0x44
#define	SEARCH_ROM	   0xF0

#define	DATA_ERR	   0xFE
#define PRESENCE_ERR 0xFF
#define SEARCH_FIRST 0xFF
#define LAST_DEVICE	0x00

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <stdint.h>
#include <string.h>

typedef uchar (CB_IO)(uchar bit);

uchar bitIo(uchar bit)
{
   uchar u8Value = 0U;

   cli();
   DDRD  |= (1<<DDD6);	/* configure output */
   _delay_us(1);

   if (1U == bit)
   {
      DDRD &= ~(1<<DDD6); /* configure input */
   }
   _delay_us(15-1);

   u8Value = (PIND & (1<<PIND6)) >> PIND6; /* read bit */

   _delay_us(60-15);

   DDRD &= ~(1<<DDD6); /* configure input */
   sei();

   return u8Value;
}

void writeByte(uchar u8Byte, CB_IO cbIo)
{
   for (int i=0; i < 8; i++)
   {
      (void)cbIo((u8Byte & 1));
      u8Byte >>= 1;
   }
}

uchar readByte()
{
   uchar u8byte = 0U;

   for (int i=0; i < 8; i++)
   {
      u8byte |= (bitIo(1) & 1) << i;
   }

   return u8byte;
}

uchar reset(void)
{
   uchar u8PresentBit;
   /* send reset pulse */
   PORTD &= ~(1<<PD6); /* set low */
   DDRD  |= (1<<DDD6);	/* configure output */
   _delay_us(480);

   cli();
   DDRD &= ~(1<<DDD6);	/* configure input */
   _delay_us(66);

   u8PresentBit = PIND & (1<<PIND6);
   sei();
   _delay_us(480-66);

   if( (PIND & (1<<PIND6)) == 0 )		// short circuit
   {
      u8PresentBit = 1;
   }

   return u8PresentBit;
}

uchar romSearch( uchar diff, uchar idata *id )
{
   uchar i, j, next_diff;
   uchar b;

   if( reset() )
   {
      return PRESENCE_ERR;			// error, no device found
   }

   writeByte(SEARCH_ROM, bitIo); // ROM search command
   next_diff = LAST_DEVICE;		// unchanged on last device
   i = 8 * 8;					      // 8 bytes
   
   do
   {
      j = 8;					      // 8 bits
      do
      {
         b = bitIo( 1 );			// read bit
         if( bitIo( 1 ) )        // read complement bit
         {
            if( b )					// 11
            return DATA_ERR;		// data error
         }
         else
         {
            if( !b )             // 00 = 2 devices
            {
               if( (diff > i) || ((*id & 1) && diff != i) )
               {
                  b = 1;			// now 1
                  next_diff = i; // next pass 0
               }
            }
         }

         (void)bitIo( b );       // write bit
         *id >>= 1;
         
         if( b )					   // store bit
         {
            *id |= 0x80;
         }

         i--;
      } while( --j );

      id++;					         // next byte
   } while( i );

   return next_diff;				   // to continue search
}

uchar sendBit(uchar bit)
{
   PORTD |= (1<<PD2);

   if (1U == bit)
   {
      _delay_us(1668);
   }
   else
   {
      _delay_us(834);
   }

   PORTD &= ~(1<<PD2);

   if (1U == bit)
   {
      _delay_us(834);
   }
   else
   {
      _delay_us(1668);
   }

   return 0U;
}

int main( void )
{
   uchar scratchpad[4];
   uchar id[16], diff, i, j;
   uchar *pId = id;

   ACSR |= (1<<7);
   wdt_disable();

   DDRD |= (1<<DDD4);	/* configure output */

   PORTD &= ~(1<<PD2);  /* switch off internal pullup */
   DDRD  |= (1<<DDD2);	/* configure output sender pin */

   PORTD &= ~(1<<PD2);  /* set output to low */

   while(1)
   {
      (void)memset(scratchpad, 0U, sizeof(scratchpad));
      (void)memset(id, 0U, sizeof(id));

      PORTD |= (1<<PD4);  /* switch led on */

      reset();
      writeByte(SKIP_ROM, bitIo);
      writeByte(CONVERT_TEMP, bitIo);
      PORTD |= (1<<PD6);   /* set high */
      DDRD  |= (1<<DDD6);  /* configure output */
      _delay_ms(750);

      i = 0U;
      for( diff = SEARCH_FIRST; diff != LAST_DEVICE; )
      {
         diff = romSearch( diff, pId );

         if( diff == PRESENCE_ERR )
         {
            break;
         }
         if( diff == DATA_ERR )
         {
            break;
         }

         if( pId[0] == 0x28 || pId[0] == 0x10 )    // temperature sensor
         {
            writeByte( READ_SCRATCHPAD, bitIo );   // read command
            scratchpad[i++] = readByte();          // low byte
            scratchpad[i++] = readByte();          // high byte
         }

         pId += 8;
      }

      pId = id;

      /* prepare sending by setting high and low for 2000µS */
      PORTD |= (1<<PD2);
      _delay_us(3000);
      PORTD &= ~(1<<PD2);
      _delay_us(6000);

      /* send preamble */
      writeByte(0xFE, sendBit);

      /* send sensor id */
      j = 0U;
      for(; j < 8U; j++)
      {
         writeByte(id[j], sendBit);
      }
      /* send temp */
      writeByte(scratchpad[0], sendBit);
      writeByte(scratchpad[1], sendBit);

      /* send sensor id */
      j = 0U;
      for(; j < 8U; j++)
      {
         writeByte(id[j], sendBit);
      }
      /* send temp */
      writeByte(scratchpad[2], sendBit);
      writeByte(scratchpad[3], sendBit);

      PORTD &= ~(1<<PD2);
      PORTD &= ~(1<<PD4); /* switch led off */

      _delay_ms(10000);
   }
   
   return 0;
}
