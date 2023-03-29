#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <syslog.h>
#include <sys/time.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <fftw3.h>
#include <stdbool.h>

#include "meteorview.h"
#include "timing.h"
#include "sdrplay_api.h"

int masterInitialised = 0;
int slaveUninitialised = 0;

int callback_count = 0;

sdrplay_api_DeviceT *chosenDevice = NULL;

// transfer->sample_count is normally 1344
//#define	SDRPLAY_BUFFER_COPY_SIZE 1344
#define	SDRPLAY_BUFFER_COPY_SIZE 2048
#define SHORT_EL_SIZE_BYTE (2)


extern bool NewFreq;                 // Set to true to indicate that frequency needs changing
extern uint32_t CentreFreq;          // Frequency in Hz from main
extern bool NewSpan;                 // Set to true to indicate that span needs changing
extern bool prepnewscanwidth;
extern bool readyfornewscanwidth;
extern bool app_exit;

extern bool NewGain;                 // Set to true to indicate that gain needs changing
extern float gain;                   // Gain (0 - 21) from main
extern int RFgain;
extern int IFgain;
extern bool agc;
extern bool Range20dB;
extern int BaseLine20dB;
extern int fft_size;                 // Number of fft samples.  Depends on scan width
extern float fft_time_smooth; 
extern uint8_t decimation_factor;           // decimation applied by SDRPlay api
extern int span; 

extern uint8_t wfalloverlap;
extern uint8_t wfallsamplefraction;

//extern pthread_t sdrplay_fft_thread_obj;

extern bool NewData; 
bool debug = false;    

int fft_offset;

uint16_t y3[2048];               // Histogram values in range 0 - 399

int force_exit = 0;

pthread_t fftThread;

double hanning_window_const[2048];

typedef struct {
	uint32_t index;
	uint32_t size;
	int16_t idata[SDRPLAY_BUFFER_COPY_SIZE * SHORT_EL_SIZE_BYTE];
	int16_t qdata[SDRPLAY_BUFFER_COPY_SIZE * SHORT_EL_SIZE_BYTE];
	pthread_mutex_t mutex;
	pthread_cond_t 	signal;
} rf_buffer2_t;

rf_buffer2_t rf_buffer2 = {
	.index = 0,
	.size = 0,
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.signal = PTHREAD_COND_INITIALIZER,
	.idata = { 0 },
	.qdata = { 0 }
};

#define FLOAT32_EL_SIZE_BYTE (4)
fftw_complex* fft_in;
fftw_complex*   fft_out;
fftw_plan   fft_plan;

pthread_mutex_t histogram;

static const char *fftw_wisdom_filename = ".fftw_wisdom";
static float fft_output_data[2048];

unsigned long long int total = 0;     /* for debug */
unsigned long long int buf_total = 0; /* for debug */


//#define FFT_BUFFER_SIZE 512
//#define FFT_BUFFER_SIZE 1024
#define FFT_BUFFER_SIZE 2048  // Size of buffer, not size of fft
#define DECIMATION 8      /* we need to do a further factor of 8 decimation to get from 80 KSPS to 10 KSPS */

long int output_buffer_i[FFT_BUFFER_SIZE];
long int output_buffer_q[FFT_BUFFER_SIZE];

sdrplay_api_RxChannelParamsT *chParams;
//sdrplay_api_DeviceT devs[6];
//sdrplay_api_DeviceParamsT *deviceParams = NULL;

int k = 0;
int m = 0;

void fft_to_buffer();
int legal_gain(int demanded_Gain);

void setup_fft(void)
{
  int i;

  // Set up FFTW
  fft_in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * fft_size);
  fft_out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * fft_size);
  i = fftw_import_wisdom_from_filename(fftw_wisdom_filename);

  // Always calculate fft plan
  i = 0;
  if (i == 0)
  {
    printf("Computing fft plan...\n");
  }
  fft_plan = fftw_plan_dft_1d(fft_size, fft_in, fft_out, FFTW_FORWARD, FFTW_EXHAUSTIVE);

  if (i == 0)
  {
    fftw_export_wisdom_to_filename(fftw_wisdom_filename);
  }
}


/* --------------------------------------------------------------------- */
void fft() {
/* --------------------------------------------------------------------- */
/* display the waterfall on the RPI 7" touch screen                      */
/* --------------------------------------------------------------------- */

    /**********************  DAVE's FFT CODE hooks into here **********************/
    /* fft() gets called when there are 1024 points in the above 2 buffers        */
    /* the buffer size is defined by   fft_size  in case 1024 isn't useful */
    /* note the output buffer is long int (int32_t) sized, can be reduced if needed         */
    /******************************************************************************/

  // Called after fft_size samples are in output_buffer_i.
  // each sample is decimated from 8 samples sent by the SDR to the CIC filter

  int i;
  pthread_mutex_lock(&rf_buffer2.mutex);
  rf_buffer2.index = 0;

  for (i = 0; i < fft_size; i++)
  {
    rf_buffer2.idata[i]  = (int16_t)(output_buffer_i[i] / 128);
    rf_buffer2.qdata[i]  = (int16_t)(output_buffer_q[i] / 128);

    //m++;
    //if (m == 10000)
    //{
    //  printf("10000 decimated samples at time %lld\n", monotonic_ms());
    //  m = 0;
   // }


    //if (i == 250)
    //{
    //  printf("i = %d, q = %d\n", rf_buffer2.idata[i], rf_buffer2.qdata[i]);
    //}    
  }

//  rf_buffer2.size = SDRPLAY_BUFFER_COPY_SIZE / (fft_size * 2);


  pthread_cond_signal(&rf_buffer2.signal);
  pthread_mutex_unlock(&rf_buffer2.mutex);
}


/* --------------------------------------------------------------------- */
void buffer_send() {
/* --------------------------------------------------------------------- */
/* send the buffer over the internet to the central server               */
/* --------------------------------------------------------------------- */
    buf_total++;   /* for debugging */
    //printf("buffer full\n"); /* for debugging */
}

void non_filter(short xi, short xq, int reset)
{
  static int buf_ptr;

output_buffer_i[buf_ptr] = (long int)xi * 256;
output_buffer_q[buf_ptr] = (long int)xq * 256;

      buf_ptr++;
      if (buf_ptr == fft_size)
      {
        //printf("Output buffer full\n");
        fft();
        buffer_send();
        buf_ptr = 0;
      }


}

/* --------------------------------------------------------------------- */
void cic_filter(short xi, short xq, int reset) {
/* --------------------------------------------------------------------- */
/* implementing a 3 stage CIC filter with R = D = 8                      */
/* for 14 bits in, and decimation by 8, we need 14 + 3.log2(8) = 23 bits */
/* this is a long int on the RPI                                         */
/* the CIC looks like:                                                   */
/*                                                                       */
/*   int1 -> int2 -> int3 -> R(down) -> comb1 -> comb2 -> comb3          */
/*                                                                       */
/* --------------------------------------------------------------------- */
  static int decimation_cnt;
  static int buf_ptr;

  static long int i_int1_out,     i_int2_out,      i_int3_out;
  static long int i_comb1_out,    i_comb2_out,     i_comb3_out;
  static long int i_int3_out_old, i_comb2_out_old, i_comb1_out_old;

  static long int q_int1_out,     q_int2_out,      q_int3_out;
  static long int q_comb1_out,    q_comb2_out,     q_comb3_out;
  static long int q_int3_out_old, q_comb2_out_old, q_comb1_out_old;

  if (reset)
  {
    printf("Got reset\n"); /* for debugging */
    /* need to reset all the filter delays and the counters */
    decimation_cnt = DECIMATION;
    buf_ptr = 0;
  
    i_int1_out  = 0;
    i_int2_out  = 0;
    i_int3_out  = 0;
    i_comb1_out = 0;
    i_comb2_out = 0;
    i_comb3_out = 0;
    i_int3_out_old  = 0;
    i_comb2_out_old = 0;
    i_comb1_out_old = 0;

    q_int1_out  = 0;
    q_int2_out  = 0;
    q_int3_out  = 0;
    q_comb1_out = 0;
    q_comb2_out = 0;
    q_comb3_out = 0;
    q_int3_out_old  = 0;
    q_comb2_out_old = 0;
    q_comb1_out_old = 0;
  }
  else
  {
    /* for efficiency we do the decimation (by factor R) first */

    //k++;
    //if (k == 80000)
    //{
    //  printf("80000 samples at time %lld\n", monotonic_ms());
    //  k = 0;
    //}

    decimation_cnt--;
    if (decimation_cnt == 0)
    {
      decimation_cnt = DECIMATION;

      /* and then the comb filters (work right to left) */
      i_comb3_out     = i_comb2_out - i_comb2_out_old;
      i_comb2_out_old = i_comb2_out;
      i_comb2_out     = i_comb1_out - i_comb1_out_old;
      i_comb1_out_old = i_comb1_out;
      i_comb1_out     = i_int3_out  - i_int3_out_old;
      i_int3_out_old  = i_int3_out;

      /* finally we have a data point to send out so off it goes */
      output_buffer_i[buf_ptr] = i_comb3_out;
     
      /* since we always do the i and q in sync, no need for separate deimation counts or buffer pointers */
      q_comb3_out     = q_comb2_out - q_comb2_out_old;
      q_comb2_out_old = q_comb2_out;
      q_comb2_out     = q_comb1_out - q_comb1_out_old;
      q_comb1_out_old = q_comb1_out;
      q_comb1_out     = q_int3_out  - q_int3_out_old;
      q_int3_out_old  = q_int3_out;
            
      output_buffer_q[buf_ptr] = q_comb3_out;

      /* if we have filled the output buffer, then we can output it to the FFT and the internet */
      //printf("start decimation count %d at time %lld\n", decimation_cnt, monotonic_ms());

      buf_ptr++;
      if (buf_ptr == fft_size)
      {
        //printf("Output buffer full\n");
        fft();
        buffer_send();
        buf_ptr = 0;
      }
    }

    /* for efficiency we do the integrators last, again right to left, so that we don't overwrite any values */
    i_int3_out = i_int2_out   + i_int3_out;
    i_int2_out = i_int1_out   + i_int2_out;
    i_int1_out = (long int)xi + i_int1_out;

    q_int3_out = q_int2_out   + q_int3_out;
    q_int2_out = q_int1_out   + q_int2_out;
    q_int1_out = (long int)xq + q_int1_out;
  }
}


/* --------------------------------------------------------------------- */
/* the data comes into this callback and we feed it out to the CIC       */
/* and then on to the FFT for display and the internet for server access */
/* --------------------------------------------------------------------- */

void StreamACallback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params,
                     unsigned int numSamples, unsigned int reset, void *cbContext)
{
  short *p_xi, *p_xq;
  int count;
  //static int samplecheck;
  
  if (reset)
  {
    printf("sdrplay_api_StreamACallback: numSamples=%d\n", numSamples);
    cic_filter(0,0, true);
  }

  /* note that the API decimation means that we only get 84 bytes each callback                */
  /* Actually seems to be 63 bytes!                                                            */
  /* We have already done API decimation to get from 2.56 MSPS down to 2.56/32 = 80 KSPS       */
  /* so now we need to do the CIC filtering to get to an audio data stream of 10 kSPS          */
  /* we do this by doing a CIC filter on each I and Q seperately, using factor of 8 decimation */

  total += numSamples;  /* for debug purposes */
  p_xi = xi;
  p_xq = xq;
  //intf ("total = %lld\n", total);

  for (count=0; count < numSamples; count++)
  {
    /* we may need to reset our CIC fiter to provide a good starting point */
    //cic_filter(*p_xi, *p_xq, reset);
    cic_filter(*p_xi, *p_xq, false);
    //non_filter(*p_xi, *p_xq, false);
    p_xi++; /* pointer maths ... ugggy but efficient */
    p_xq++;
  }


  //if (numSamples != 63)
  //{
  //  printf("numSamples = %d\n", numSamples);
  //}
  //if (samplecheck > 8000)
  //{
  //  printf ("                         8000 samples at %lld\n", monotonic_ms());
  //  samplecheck = 0;
  //}
  //samplecheck = samplecheck + numSamples;  
  return;
}


void StreamBCallback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext) {
	if (reset) printf("sdrplay_api_StreamBCallback: numSamples=%d\n", numSamples);
	// Process stream callback data here - this callback will only be used in dual tuner mode
	return;
}

void EventCallback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner, sdrplay_api_EventParamsT *params, void *cbContext)
{
  switch(eventId)
  {
    case sdrplay_api_GainChange:
      printf("sdrplay_api_EventCb: %s, tuner=%s gRdB=%d lnaGRdB=%d systemGain=%.2f\n", "sdrplay_api_GainChange",
				(tuner == sdrplay_api_Tuner_A)? "sdrplay_api_Tuner_A":"sdrplay_api_Tuner_B",
				params->gainParams.gRdB, params->gainParams.lnaGRdB,params->gainParams.currGain);
      break;
    case sdrplay_api_PowerOverloadChange:
      printf("sdrplay_api_PowerOverloadChange: tuner=%s powerOverloadChangeType=%s\n",
				(tuner == sdrplay_api_Tuner_A)? "sdrplay_api_Tuner_A": "sdrplay_api_Tuner_B",
				(params->powerOverloadParams.powerOverloadChangeType == sdrplay_api_Overload_Detected) ? "sdrplay_api_Overload_Detected":"sdrplay_api_Overload_Corrected");
			// Send update message to acknowledge power overload message received
			sdrplay_api_Update(chosenDevice->dev, tuner, sdrplay_api_Update_Ctrl_OverloadMsgAck,sdrplay_api_Update_Ext1_None);
      break;
    case sdrplay_api_RspDuoModeChange:
      printf("sdrplay_api_EventCb: %s, tuner=%s modeChangeType=%s\n",
				"sdrplay_api_RspDuoModeChange", (tuner == sdrplay_api_Tuner_A)?
				"sdrplay_api_Tuner_A": "sdrplay_api_Tuner_B",
				(params->rspDuoModeParams.modeChangeType == sdrplay_api_MasterInitialised)?
				"sdrplay_api_MasterInitialised":
				(params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveAttached)?
				"sdrplay_api_SlaveAttached":
				(params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveDetached)?
				"sdrplay_api_SlaveDetached":
				(params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveInitialised)?
				"sdrplay_api_SlaveInitialised":
				(params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveUninitialised)?
				"sdrplay_api_SlaveUninitialised":
				(params->rspDuoModeParams.modeChangeType == sdrplay_api_MasterDllDisappeared)?
				"sdrplay_api_MasterDllDisappeared":
				(params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveDllDisappeared)?
				"sdrplay_api_SlaveDllDisappeared": "unknown type");
			if (params->rspDuoModeParams.modeChangeType == sdrplay_api_MasterInitialised)
				masterInitialised = 1;
			if (params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveUninitialised)
				slaveUninitialised = 1;
      break;
    case sdrplay_api_DeviceRemoved:
      printf("sdrplay_api_EventCb: %s\n", "sdrplay_api_DeviceRemoved");
      break;
    default:
      printf("sdrplay_api_EventCb: %d, unknown event\n", eventId);
    break;
  }
}


static void close_fftw(void)
{
    /* De-init fftw */
    fftw_free(fft_in);
    fftw_free(fft_out);
    fftw_destroy_plan(fft_plan);
    fftw_forget_wisdom();
}


/* transfer->sample_count is normally 65536 */
#define	AIRSPY_BUFFER_COPY_SIZE	65536

typedef struct {
	uint32_t index;
	uint32_t size;
	char data[AIRSPY_BUFFER_COPY_SIZE * FLOAT32_EL_SIZE_BYTE];
	pthread_mutex_t mutex;
	pthread_cond_t 	signal;
} rf_buffer_t;

rf_buffer_t rf_buffer = {
	.index = 0,
	.size = 0,
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.signal = PTHREAD_COND_INITIALIZER,
	.data = { 0 }
};


typedef struct {
	float data[2048];
	pthread_mutex_t mutex;
} fft_buffer_t;

fft_buffer_t fft_buffer = {
	.mutex = PTHREAD_MUTEX_INITIALIZER,
};

// FFT Thread
void *thread_fft(void *dummy)
{
  (void) dummy;
  int i;
  int offset;
  uint8_t pass;
  fftw_complex pt;
  double pwr;
  double lpwr;
  double pwr_scale;
  float fft_circ[4096][2];
  int circ_write_index = 0;
  int circ_read_pos = 0;
  int window_open;
  int window_close;

  pwr_scale = 1.0 / ((float)fft_size * (float)fft_size);       // App is restarted on change of fft size

  while (true)
  {
    // Calculate the time definition window
    window_open = fft_size / 2 - fft_size / (2 * wfallsamplefraction);
    window_close = fft_size / 2 + fft_size / (2 * wfallsamplefraction);
    if(debug)
    {
      printf("Window open at %d, Window Close at %d\n", window_open, window_close);
    }

    for(pass = 0; pass < 2 * wfalloverlap; pass++)           // Go round this loop twice the overlap, once round the overlap for each input write
    {
      if ((pass == 0) || (pass == wfalloverlap))          // So this pass needs an input write
      {
        // Lock input buffer
        pthread_mutex_lock(&rf_buffer2.mutex);

        // Wait for signalled input
        pthread_cond_wait(&rf_buffer2.signal, &rf_buffer2.mutex);

        if (pass == 0)                            // First and odd numbered input writes
        {
          circ_write_index = 1;
        }
        else
        {
          circ_write_index = 0;
        }
          

        // offset = rf_buffer2.index * fft_size * 2;
        offset = 0;

        // Copy data out of rf buffer into fft_circ buffer in alternate stripes
        for (i = 0; i < fft_size; i++)
        {
          //fft_circ[i + circ_write_index * fft_size][0] = (float)(rf_buffer2.idata[offset+(i)]) * hanning_window_const[i];
          //fft_circ[i + circ_write_index * fft_size][1] = (float)(rf_buffer2.qdata[offset+(i)]) * hanning_window_const[i];
          fft_circ[i + circ_write_index * fft_size][0] = (float)(rf_buffer2.idata[offset+(i)]);
          fft_circ[i + circ_write_index * fft_size][1] = (float)(rf_buffer2.qdata[offset+(i)]);
        }

        if (debug)
        {
          printf("Write stripe %d\n", circ_write_index);
        }

	    rf_buffer2.index++;

	    // Unlock input buffer
        pthread_mutex_unlock(&rf_buffer2.mutex);
      }                                                       // End of input write

      // Add delay between overlap passes
      if ((pass != 0) && (pass != wfalloverlap))
      {
        usleep((int)((80000 * fft_size) / (wfalloverlap * 1000)));  
      }

      //printf("delay = %d \n", (int)((80000 * fft_size) / (wfalloverlap * 1000)));

      // Now read it out of the buffer 
      for (i = 0; i < fft_size; i++)
      {
        circ_read_pos = i + fft_size * pass / wfalloverlap;
        if (circ_read_pos > 2 * fft_size)
        {
        circ_read_pos = circ_read_pos - 2 * fft_size;
        }

        fft_in[i][0] = fft_circ[circ_read_pos][0] * hanning_window_const[i];
        fft_in[i][1] = fft_circ[circ_read_pos][1] * hanning_window_const[i];

        // debug printing block
        if ((i == 0) && (debug))
        {
          printf("pass %d: ", pass);
        }
        if (((i == 0) || (i == fft_size / 4 - 1 ) || (i == fft_size / 4) || (i == fft_size / 2 - 1 )
          || (i == fft_size / 2) || (i == fft_size * 3 / 4 - 1 ) || (i == fft_size * 3 / 4)) && (debug))
        {
          printf("%d - ", circ_read_pos);
        }
        if ((i == fft_size - 1) && (debug))
        {
          printf("%d\n", circ_read_pos);
        }

        // Apply input time window for increased time definition
        if ((i < window_open) || (i > window_close))
        {
          fft_in[i][0] = 0;
          fft_in[i][1] = 0;
        }
	  }

      if(debug)
      {
        printf("Window open at %d, Window Close at %d\n", window_open, window_close);
      }

      // Run FFT
      fftw_execute(fft_plan);

      // Take the output of the fft, convert it to dB and smooth it.

      // Lock output buffer
      pthread_mutex_lock(&fft_buffer.mutex);

      for (i = 0; i < fft_size; i++)
	  {
	    // shift, normalize and convert to dBFS
	    if (i < fft_size / 2)
	    {
	      pt[0] = fft_out[fft_size / 2 + i][0] / fft_size;
	      pt[1] = fft_out[fft_size / 2 + i][1] / fft_size;
	    }
	    else
	    {
          pt[0] = fft_out[i - fft_size / 2][0] / fft_size;
	      pt[1] = fft_out[i - fft_size / 2][1] / fft_size;
	    }

	    pwr = pwr_scale * (pt[0] * pt[0]) + (pt[1] * pt[1]);

        lpwr = 10.f * log10(pwr + 1.0e-20);

        fft_buffer.data[i] = (lpwr * (1.f - fft_time_smooth)) + (fft_buffer.data[i] * fft_time_smooth);
      }

      // Unlock output buffer
      pthread_mutex_unlock(&fft_buffer.mutex);

      fft_to_buffer();

    }    // End of the read cycle
  }
  return NULL;
}


// Scale and manage the fft data
void fft_to_buffer()
{
  int32_t j;
  uint32_t average = 0;

  // Lock FFT output buffer for reading
  pthread_mutex_lock(&fft_buffer.mutex);

  for (j = 0; j < fft_size; j++)
  {
    fft_output_data[j] = fft_buffer.data[j];
  }

  // Unlock FFT output buffer
  pthread_mutex_unlock(&fft_buffer.mutex);

  // Calculate the centre of the fft
  // for 512, 1024 2048 etc = fft_size / 2 - 256
  // for 500, 1000, 2000 = fft_size / 2 - 250  // 500 -> -7, 1000 -> 250 - 7, 2000 750 - 7
  if (fft_size == 500)
  {
    fft_offset = -7;
  }
  if (fft_size == 512)
  {
    fft_offset = 0;
  }
  if (fft_size == 1000)
  {
    fft_offset = 243;
  }
  if (fft_size == 1024)
  {
    fft_offset = 256;
  }
  if (fft_size == 2000)
  {
    fft_offset = 743;
  }
  if (fft_size == 2048)
  {
    fft_offset = 768;
  }


  // Scale and limit the samples
  for(j = 0; j < fft_size; j++)
  {
    // Add a constant to position the baseline
    fft_output_data[j] = fft_output_data[j] + 20.0;

    // Multiply by 5 (pixels per dB on display)
    fft_output_data[j] = fft_output_data[j] * 5;

    if (Range20dB) // Range20dB
    {
                
      fft_output_data[j] = fft_output_data[j] - 5 * (80 + BaseLine20dB);  
      fft_output_data[j] = 4 * fft_output_data[j];
    }

    if (fft_output_data[j] > 399)
    {
      //printf("fft output = %f\n", fft_output_data[j] );
      fft_output_data[j] = 399;
    }
    if (fft_output_data[j] < 1)
    {
      //printf("fft output = %f\n", fft_output_data[j] );
      fft_output_data[j] = 1;
    }
    average = average + fft_output_data[j];
  }
  if(debug)
  {
    printf("Average = %d\n", average);
  }

  // y3 needs valid values from [6] to [506]
  // Only [7] through [505] are displayed (499 columns)
  // [6] is lowest cal line
  // [256] is middle cal line
  // [506] is highest cal line

  // Lock the histogram buffer for writing
  pthread_mutex_lock(&histogram);
  
  y3[6] = fft_output_data[7 + fft_offset];

  for (j = 7; j <= 505; j++)
  {
    y3[j] = fft_output_data[j + fft_offset];
    //printf("%d\n", j + fft_offset);
  }

  y3[506] = fft_output_data[505 + fft_offset];

  // Unlock the histogram buffer
  pthread_mutex_unlock(&histogram);

  // Wait here until data has been read
  NewData = true;
  while (NewData == true)
  {
    usleep(100);
  }
}

// Takes demanded gain and returns a valid gain for the device
int legal_gain(int demanded_Gain)
{
  if (chosenDevice->hwVer == SDRPLAY_RSP1_ID)
  {
    if (demanded_Gain > 3)
    {
      return 3;
    }
    else
    {
      return demanded_Gain;
    }
  }
  if (chosenDevice->hwVer == SDRPLAY_RSP1A_ID)
  {
    if (demanded_Gain > 6)
    {
      return 6;
    }
    else
    {
      return demanded_Gain;
    }
  }
  if (chosenDevice->hwVer == SDRPLAY_RSP2_ID)
  {
    if (demanded_Gain > 4)
    {
      return 4;
    }
    else
    {
      return demanded_Gain;
    }
  }
  if (chosenDevice->hwVer == SDRPLAY_RSPduo_ID)
  {
    if (demanded_Gain > 4)
    {
      return 4;
    }
    else
    {
      return demanded_Gain;
    }
  }
  if (chosenDevice->hwVer == SDRPLAY_RSPdx_ID)
  {
    if (demanded_Gain > 18)
    {
      return 18;
    }
    else
    {
      return demanded_Gain;
    }
  }
  return 0;
}


// Main thread
void *sdrplay_fft_thread(void *arg)
{
  bool *exit_requested = (bool *)arg;
  int i;
  uint64_t last_output;
  //sdrplay_api_RxChannelParamsT *chParams;
  sdrplay_api_DeviceT devs[6];
  unsigned int ndev;
  float ver = 0.0;
  sdrplay_api_ErrT err;
  sdrplay_api_DeviceParamsT *deviceParams = NULL;
  sdrplay_api_CallbackFnsT cbFns;
  int reqTuner = 0;
  int master_slave = 0;
  unsigned int chosenIdx = 0;

  // Initialise fft
  printf("Initialising FFT (%d bin).. \n", fft_size);
  setup_fft();
  for (i = 0; i < fft_size; i++)
  {
    hanning_window_const[i] = 0.5 * (1.0 - cos(2*M_PI*(((double)i)/fft_size)));
  }
  printf("FFT Intitialised\n");

  printf("requested Tuner=%c Mode=%s\n", (reqTuner == 0)? 'A': 'B', (master_slave == 0)?
    "Single_Tuner": "Master/Slave");

  // Open API
  if ((err = sdrplay_api_Open()) != sdrplay_api_Success)
  {
    printf("sdrplay_api_Open failed %s\n", sdrplay_api_GetErrorString(err));
    cleanexit(129);
  }
  else
  {
    // Enable debug logging output
    //if ((err = sdrplay_api_DebugEnable(NULL, 1)) != sdrplay_api_Success)
    //{
    //  printf("sdrplay_api_DebugEnable failed %s\n", sdrplay_api_GetErrorString(err));
    //}

    // Check API versions match
    if ((err = sdrplay_api_ApiVersion(&ver)) != sdrplay_api_Success)
    {
      printf("sdrplay_api_ApiVersion failed %s\n", sdrplay_api_GetErrorString(err));
      cleanexit(129);
    }

    if (ver != SDRPLAY_API_VERSION)
    {
      printf("API version don't match (local=%.2f dll=%.2f)\n", SDRPLAY_API_VERSION, ver);
      // goto CloseApi;
    }
 
    // Lock API while device selection is performed
    sdrplay_api_LockDeviceApi();

    // Fetch list of available devices
    if ((err = sdrplay_api_GetDevices(devs, &ndev, sizeof(devs) / sizeof(sdrplay_api_DeviceT))) != sdrplay_api_Success)
    {
      printf("sdrplay_api_GetDevices failed %s\n", sdrplay_api_GetErrorString(err));
      cleanexit(129);
    }

    // Exit if no devices found
    if (ndev == 0)
    {
      printf("No devices found.  Exiting\n");
      cleanexit(129);
    }

    printf("MaxDevs=%d NumDevs=%d\n", sizeof(devs) / sizeof(sdrplay_api_DeviceT), ndev);
    if (ndev > 0)
    {
      for (i = 0; i < (int)ndev; i++)
      {
        if (devs[i].hwVer == SDRPLAY_RSPduo_ID)
        {
          printf("Dev%d: SerNo=%s hwVer=%d tuner=0x%.2x rspDuoMode=0x%.2x\n", i,devs[i].SerNo, devs[i].hwVer , devs[i].tuner, devs[i].rspDuoMode);
        }
        else
        {
          printf("Dev%d: SerNo=%s hwVer=%d tuner=0x%.2x\n", i, devs[i].SerNo,devs[i].hwVer, devs[i].tuner);
        }
      }
		
      // Choose device
      if ((reqTuner == 1) || (master_slave == 1))  // requires RSPduo
      {
        // Pick first RSPduo
        for (i = 0; i < (int)ndev; i++)
        {
          if (devs[i].hwVer == SDRPLAY_RSPduo_ID)
          {
            chosenIdx = i;
            break;
          }
        }
      }
      else
      {
        // Pick first device of any type
        for (i = 0; i < (int)ndev; i++)
        {
          chosenIdx = i;
          break;
        }
      }
      if (i == ndev)
      {
        printf("Couldn't find a suitable device to open - exiting\n");
        // goto UnlockDeviceAndCloseApi;
      }
      printf("chosenDevice = %d\n", chosenIdx);
      chosenDevice = &devs[chosenIdx];
 
      // If chosen device is an RSPduo, assign additional fields
      if (chosenDevice->hwVer == SDRPLAY_RSPduo_ID)
      {
        // If master device is available, select device as master
        if (chosenDevice->rspDuoMode & sdrplay_api_RspDuoMode_Master)
        {
          // Select tuner based on user input (or default to TunerA)
          chosenDevice->tuner = sdrplay_api_Tuner_A;
          if (reqTuner == 1)
          {
            chosenDevice->tuner = sdrplay_api_Tuner_B;
          }
          // Set operating mode
          if (!master_slave)  // Single tuner mode
          {
            chosenDevice->rspDuoMode = sdrplay_api_RspDuoMode_Single_Tuner;
            printf("Dev%d: selected rspDuoMode=0x%.2x tuner=0x%.2x\n", chosenIdx,chosenDevice->rspDuoMode, chosenDevice->tuner);
          }
          else
          {
            chosenDevice->rspDuoMode = sdrplay_api_RspDuoMode_Master;
            // Need to specify sample frequency in master/slave mode
            chosenDevice->rspDuoSampleFreq = 6000000.0;
            printf("Dev%d: selected rspDuoMode=0x%.2x tuner=0x%.2x rspDuoSampleFreq=%.1f\n",
              chosenIdx, chosenDevice->rspDuoMode,
              chosenDevice->tuner, chosenDevice->rspDuoSampleFreq);
          }
        }
        else   // Only slave device available
        {
          // Shouldn't change any parameters for slave device
        }
      }
 
      // Select chosen device
      if ((err = sdrplay_api_SelectDevice(chosenDevice)) != sdrplay_api_Success)
      {
        printf("sdrplay_api_SelectDevice failed %s\n", sdrplay_api_GetErrorString(err));
        cleanexit(129);
      }
		
      // Unlock API now that device is selected
      sdrplay_api_UnlockDeviceApi();
			
      // Retrieve device parameters so they can be changed if wanted
      if ((err = sdrplay_api_GetDeviceParams(chosenDevice->dev, &deviceParams)) != sdrplay_api_Success)
      {
        printf("sdrplay_api_GetDeviceParams failed %s\n",sdrplay_api_GetErrorString(err));
        cleanexit(129);
      }
		
      // Check for NULL pointers before changing settings
      if (deviceParams == NULL)
      {
        printf("sdrplay_api_GetDeviceParams returned NULL deviceParams pointer\n");
        cleanexit(129);
      }
		
      // Configure dev parameters
      if (deviceParams->devParams != NULL)
      {
        // This will be NULL for slave devices, only the master can change these parameters
        // Only need to update non-default settings
        if (master_slave == 0)
        {
          // we choose to sample at 256 * 10 KSPS to make decimation to 10 KSPS easier
          deviceParams->devParams->fsFreq.fsHz =   2560000.0;
        }
        else
        {
          // Can't change Fs in master/slave mode
        }
      }
      // Configure tuner parameters (depends on selected Tuner which parameters to use)
      chParams = (chosenDevice->tuner == sdrplay_api_Tuner_B)? deviceParams->rxChannelB: deviceParams->rxChannelA;
      if (chParams != NULL)
      {
        chParams->tunerParams.rfFreq.rfHz = (float)CentreFreq;

        // Set the smallest bandwidth as we are going to narrow it down a lot more soon
        chParams->tunerParams.bwType = sdrplay_api_BW_0_200;

        if (master_slave == 0)  // Change single tuner mode to ZIF
        {
          chParams->tunerParams.ifType = sdrplay_api_IF_Zero;
        }
        chParams->tunerParams.gain.gRdB = IFgain;          // Set between 20 and 59.
        chParams->tunerParams.gain.LNAstate = legal_gain(RFgain);

        // --------------------------------  setup the Decimation to max ------------------------------
        chParams->ctrlParams.decimation.enable = 1;            // default 0 (off), 1=on
        chParams->ctrlParams.decimation.decimationFactor = decimation_factor; // default 1, max 32
        // wideband = 1 uses better filters but less efficient on cpu useage
        chParams->ctrlParams.decimation.wideBandSignal = 1;    // default 0
			
        // Control AGC
        if (agc == true)
        {
          chParams->ctrlParams.agc.enable = sdrplay_api_AGC_100HZ;
        }
        else
        {
          chParams->ctrlParams.agc.enable = sdrplay_api_AGC_DISABLE;
        }

      }
      else
      {
        printf("sdrplay_api_GetDeviceParams returned NULL chParams pointer\n");
        // goto CloseApi;
      }

		
      // Assign callback functions to be passed to sdrplay_api_Init()
      cbFns.StreamACbFn = StreamACallback;
      cbFns.StreamBCbFn = StreamBCallback;
      cbFns.EventCbFn = EventCallback;
		
      // Now we're ready to start by calling the initialisation function
      // This will configure the device and start streaming
      if ((err = sdrplay_api_Init(chosenDevice->dev, &cbFns, NULL)) != sdrplay_api_Success)
      {
        printf("sdrplay_api_Init failed %s\n", sdrplay_api_GetErrorString(err));
        if (err == sdrplay_api_StartPending)
        { // This can happen if we're starting inmaster/slave mode as a slave and the master is not yet running
          while(1)
          {
            usleep(1000);
            if (masterInitialised)  // Keep polling flag set in event callback until the master is initialised
            {
              // Redo call - should succeed this time
              if ((err = sdrplay_api_Init(chosenDevice->dev, &cbFns, NULL)) != sdrplay_api_Success)
              {
                printf("sdrplay_api_Init failed %s\n", sdrplay_api_GetErrorString(err));
              }
              //goto CloseApi;
            }
            printf("Waiting for master to initialise\n");
          }
        }
        else
        {
          sdrplay_api_ErrorInfoT *errInfo = sdrplay_api_GetLastError(NULL);
          if (errInfo != NULL) printf("Error in %s: %s(): line %d: %s\n", errInfo->file, errInfo->function, errInfo->line, errInfo->message);
          //goto CloseApi;
        }
      }
    }
  }
  
  // Set initial parameters
  last_output = monotonic_ms();

  fft_offset = (fft_size / 2) - 250;

  // zero all the buffers
  for(i = 0; i < 2048; i++)
  {
    y3[i] = 1;
    //hanning_window_const[i] = 0;
    fft_buffer.data[i] = -20.0;
    fft_output_data[i] = 1;
  }

  // Start fft thread
  printf("Starting FFT Thread.. \n");
  if (pthread_create(&fftThread, NULL, thread_fft, NULL))
  {
    printf("Error creating FFT thread\n");
    //return -1;
  }
  pthread_setname_np(fftThread, "FFT Calculation");
  printf("FFT thread running.\n");

  // Copy fft scaled data to display buffer 
  while ((false == *exit_requested) && (app_exit == false)) 
  {
    if(monotonic_ms() > (last_output + 50))  // so 20 Hz refresh
    {
      //fft_to_buffer();

      // Reset timer for 20 Hz refresh
      last_output = monotonic_ms();

      // Check for parameter changes (loop ends here if none)

      // Change of Frequency
      if (NewFreq == true)
      {
        sdrplay_api_Uninit(chosenDevice->dev);
        chParams->tunerParams.rfFreq.rfHz = (float)CentreFreq;

        if ((err = sdrplay_api_Init(chosenDevice->dev, &cbFns, NULL)) != sdrplay_api_Success)
        {
          printf("sdrplay_api_Init failed %s\n", sdrplay_api_GetErrorString(err));
        }
        NewFreq = false;
      }

      // Change of gain
      if (NewGain == true)
      {
        sdrplay_api_Uninit(chosenDevice->dev);

        // Set AGC
        if (agc == true)
        {
          chParams->ctrlParams.agc.enable = sdrplay_api_AGC_100HZ;
        }
        else
        {
          chParams->ctrlParams.agc.enable = sdrplay_api_AGC_DISABLE;
        }

        // Set RF Gain
        chParams->tunerParams.gain.LNAstate = legal_gain(RFgain);

        // Set IF gain
        chParams->tunerParams.gain.gRdB = IFgain;          // Set between 20 (max gain) and 59 (least)
        
        if ((err = sdrplay_api_Init(chosenDevice->dev, &cbFns, NULL)) != sdrplay_api_Success)
        {
          printf("sdrplay_api_Init failed %s\n", sdrplay_api_GetErrorString(err));
        }
        NewGain = false;
      }

      // Change of Display Span
      if (prepnewscanwidth == true)
      {

  //printf("Waiting for SDR Play FFT Thread to exit..\n");
  //pthread_join(sdrplay_fft_thread_obj, NULL);
  //printf("FFT Thread exited\n");



        printf("prepnewscanwidth == true\n");
        // Notify touchscreen that parameters can be changed
        readyfornewscanwidth = true;
        printf("readyfornewscanwidth == true\n");

        // Wait for new parameters to be calculated
        while (NewSpan == false)
        {
          usleep(100);
        }
        printf("NewSpan == true\n");

  if (fft_size == 500)
  {
    fft_offset = -7;
  }
  if (fft_size == 512)
  {
    fft_offset = 0;
  }
  if (fft_size == 1000)
  {
    fft_offset = 243;
  }
  if (fft_size == 1024)
  {
    fft_offset = 256;
  }
  if (fft_size == 2000)
  {
    fft_offset = 743;
  }
  if (fft_size == 2048)
  {
    fft_offset = 768;
  }
        printf("new fft_size = %d\n", fft_size);
        printf("new fft_offset = %d\n", fft_offset);


        // Reset trigger parameters
        NewSpan = false;
        prepnewscanwidth = false;
        readyfornewscanwidth = false;

  // Initialise fft
  printf("Initialising FFT (%d bin).. \n", fft_size);
  setup_fft();
  for (i = 0; i < fft_size; i++)
  {
    hanning_window_const[i] = 0.5 * (1.0 - cos(2*M_PI*(((double)i)/fft_size)));
  }
  printf("FFT Intitialised\n");



  //printf("Starting FFT Thread.. \n");
  //if (pthread_create(&fftThread, NULL, thread_fft, NULL))
  //{
  //  printf("Error creating FFT thread\n");
    //return -1;
  //}
  //pthread_setname_np(fftThread, "FFT Calculation");
  //printf("FFT thread running.\n");


        printf("end of change\n");

      }
    }
    if (*exit_requested == true)
    {
      printf("Exit Requested before delay\n");
    }
    sleep_ms(1);
  }

  // On exit
  sdrplay_api_Uninit(chosenDevice->dev);
  sdrplay_api_ReleaseDevice(chosenDevice);
  sdrplay_api_UnlockDeviceApi();
  sdrplay_api_Close();

  close_fftw();
  closelog();
  printf("Main fft Thread Closed\n");

  return NULL;
}
