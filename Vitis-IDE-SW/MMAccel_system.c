#include <stdio.h>
#include <stdlib.h>
#include "xaxidma.h"
#include "xparameters.h"
#include "platform.h"
#include <xtime_l.h>  // Timer for execution time calculations
#include "xil_printf.h"

#define EXPT 1
#define Mat_Dtype float
#define MATSIZE 8


void matrixmul_benchmark(Mat_Dtype input_A[MATSIZE][MATSIZE], Mat_Dtype input_B[MATSIZE][MATSIZE], Mat_Dtype output_C[MATSIZE][MATSIZE]);
unsigned int float_to_u32(float val)
{
	unsigned int result;
	union float_bytes {
		float v;
		unsigned char bytes[4];
	} data;

	data.v = val;

	result = (data.bytes[3] << 24) + (data.bytes[2] << 16) + (data.bytes[1] << 8) + (data.bytes[0]);

	return result;
}

unsigned int u32_to_float(unsigned int val)
{
	union {
		float val_float;
		unsigned char bytes[4];
	} data;

	data.bytes[3] = (val >> (8*3)) & 0xff;
	data.bytes[2] = (val >> (8*2)) & 0xff;
	data.bytes[1] = (val >> (8*1)) & 0xff;
	data.bytes[0] = (val >> (8*0)) & 0xff;

	return data.val_float;
}

int Matrixmul()
{
	int status;
	int row, col;
	float time_processor=0;
	float time_FPGA=0;
	float curr_time=0;

	Mat_Dtype input_A[MATSIZE][MATSIZE], input_B[MATSIZE][MATSIZE], output_C_SW[MATSIZE][MATSIZE];
	Mat_Dtype output_C_HW[MATSIZE][MATSIZE];
	Mat_Dtype DMA_output[MATSIZE*MATSIZE];
	Mat_Dtype DMA_input[2*MATSIZE*MATSIZE];
//	u32 DMA_output[MATSIZE*MATSIZE];
//	u32 DMA_input[2*MATSIZE*MATSIZE];


	////////////////////////////////////////////////////////////////////////////////////////////
	// ACP DMA 0 Initialization
	XAxiDma_Config *DMA_confptr0; // DMA configuration pointer
	XAxiDma AxiDMA0; // DMA instance pointer
	// Copy the DMA information (received from hardware in xparameters.h file)
	DMA_confptr0 = XAxiDma_LookupConfig(XPAR_AXI_DMA_0_DEVICE_ID);
	status = XAxiDma_CfgInitialize(&AxiDMA0, DMA_confptr0);
	if(status != XST_SUCCESS)
	{
		printf("DMA 0 Init Failed\t\n");
		return XST_FAILURE;
	}
	////////////////////////////////////////////////////////////////////////////////////////////
	// Performance Evaluations Start
	for(int k=0; k<EXPT; k++)
	{
		XTime seed_value;
		XTime_GetTime(&seed_value);
		srand(seed_value);

		// /////////////////////////////////////////////////////////////////////////////////////
		// Generate test data
		float initData = 0.0;
		for(row=0; row<MATSIZE; row++)
		{
			for(col=0; col<MATSIZE; col++)
			{
//				input_A[row][col] = ((float) rand()/(RAND_MAX/5));
//				input_B[row][col] = ((float) rand()/(RAND_MAX/5));
				input_A[row][col] = ((float) initData);
				input_B[row][col] = ((float) initData);
				initData += 1.0;
			}
		}

		// to store the time at which certain processes starts and ends
		XTime time_PS_start, time_PS_end;
		XTime time_PL_start, time_PL_end;  // PL time calculations

		////////////////////////////////////////////////////////////////////////////////////////
		// Performance Evaluation for SW Matrix Multiplication
		XTime_SetTime(0);
		XTime_GetTime(&time_PS_start);
		// Call software benchmark function
		matrixmul_benchmark(input_A, input_B, output_C_SW);
		XTime_GetTime(&time_PS_end);  // Capture the timer value at the end
		curr_time = ((float) 1.0*(time_PS_end - time_PS_start) / (COUNTS_PER_SECOND/1000000));
		time_processor = time_processor + curr_time;
		printf("/**********************************\t\n");
		printf("Execution Time for PS in Micro-Seconds for %d iteration: %f\t\n", k, curr_time);


		////////////////////////////////////////////////////////////////////////////////////////
		// Generate DMA Input
		int index=0;
		for(row=0; row<MATSIZE; row++)
		{
			for(col=0; col<MATSIZE; col++)
			{
//				DMA_input[index] = float_to_u32(input_A[row][col]);
				DMA_input[index] = input_A[row][col];
				index = index+1;
			}
		}
//		printf("****** From A, DMA_input[0] = %f\n", DMA_input[0]);

		for(row=0; row<MATSIZE; row++)
		{
			for(col=0; col<MATSIZE; col++)
			{
//				DMA_input[index] = float_to_u32(input_B[row][col]);
				DMA_input[index] = input_B[row][col];
				index = index+1;
			}
		}
//		printf("****** From B, DMA_input[0] = %f\n", DMA_input[0]);

		////////////////////////////////////////////////////////////////////////////////////////
		// Matrix Multiplication on PL using DMA 0
		XTime_SetTime(0);
		XTime_GetTime(&time_PL_start);

		// Cache Flush
		Xil_DCacheFlushRange((UINTPTR)DMA_input, (sizeof(Mat_Dtype)*MATSIZE*MATSIZE*2));
		Xil_DCacheFlushRange((UINTPTR)DMA_output, (sizeof(Mat_Dtype)*MATSIZE*MATSIZE));

		status = XAxiDma_SimpleTransfer(&AxiDMA0, (UINTPTR)DMA_output, (sizeof(Mat_Dtype)*MATSIZE*MATSIZE), XAXIDMA_DEVICE_TO_DMA);
		if (status != XST_SUCCESS) {
			return XST_FAILURE;
		}
		status = XAxiDma_SimpleTransfer(&AxiDMA0, (UINTPTR)DMA_input, (sizeof(Mat_Dtype)*MATSIZE*MATSIZE*2), XAXIDMA_DMA_TO_DEVICE);
		if (status != XST_SUCCESS) {
			return XST_FAILURE;
		}

		//printf("DMA Transfer Started... !!!\n");

		//  We have only configure the DMA to perform these two transactions.
		// DMA might not have started the transactions.
		// -
		// (MM2S_DMASR (MM2S DMA Status Register – Offset 04h)
		// 04h  	MM2S_DMASR		MM2S DMA Status register
		// Bit 0: DMA Channel Halted. Indicates the run/stop state of the DMA channel.
		//		• 0 = DMA channel running.
		//		• 1 = DMA channel halted.
		// Bit 1: DMA Channel Idle. Indicates the state of AXI DMA operations.
		//		• 0 = Not Idle.
		//		• 1 = Idle.
		status = XAxiDma_ReadReg(XPAR_AXI_DMA_0_BASEADDR, 0x04) & 0x00000002;
		while(status!=0x00000002) // while ( status != Idle )
		{
			status = XAxiDma_ReadReg(XPAR_AXI_DMA_0_BASEADDR, 0x04) & 0x00000002;
		}
		//printf("MM2S DMA Transfer Completed... !!!\n");

		// --
		// S2MM_DMASR (S2MM DMA Status Register – Offset 34h)
		// 34h		S2MM_DMASR		S2MM DMA Status register
		status = XAxiDma_ReadReg(XPAR_AXI_DMA_0_BASEADDR, 0x34) & 0x00000002;
		while(status!=0x00000002)
		{
			status = XAxiDma_ReadReg(XPAR_AXI_DMA_0_BASEADDR, 0x34) & 0x00000002;
		}
		//printf("S2MM DMA Transfer Completed... !!!\n");

		XTime_GetTime(&time_PL_end);
		curr_time = ((float)1.0 * (time_PL_end - time_PL_start) / (COUNTS_PER_SECOND/1000000));
		time_FPGA = time_FPGA + curr_time;
		printf("Execution Time for Non-Optimized MMUL in PL in Micro-Seconds for %d iteration: %f\t\n", k, curr_time);

		index = 0;
		for(row=0; row<MATSIZE; row++)
		{
			for(col=0; col<MATSIZE; col++)
			{
//				output_C_HW[row][col] = u32_to_float(DMA_output[index]);
				output_C_HW[row][col] = DMA_output[index];
				index = index + 1;
			}
		}

		// Compare Benchmark and hardware function output
		// Receive stream output C from hardware function
		for(row=0; row<MATSIZE; row++)
		{
			for(col=0; col<MATSIZE; col++)
			{
				float diff = abs(output_C_HW[row][col]-output_C_SW[row][col]);
				if(diff > 0.001)
				{
					printf("Non-Optimized MMUL Error at row %d and col %d\t\n", row, col);
					printf("Hardware output %f\r\n", output_C_HW[row][col]);
					printf("Software output %f\r\n", output_C_SW[row][col]);
					break;
				}
			}
		}

	} // k < EXPT

	printf("---> Execution Time for PS in Micro-Seconds: %f \t\n", time_processor/EXPT);
	printf("---> Average Execution Time for Non-Optimized MMUL in PL in Micro-Seconds: %f \t\n", time_FPGA/EXPT);

	return 0;
}

int main()
{
	init_platform();
	Matrixmul();
	cleanup_platform();
	return 0;
}

void matrixmul_benchmark(Mat_Dtype input_A[MATSIZE][MATSIZE], Mat_Dtype input_B[MATSIZE][MATSIZE], Mat_Dtype output_C[MATSIZE][MATSIZE])
{
	for(int row=0; row<MATSIZE; row++)
	{
		for(int col=0; col<MATSIZE; col++)
		{
			float res=0;
			for(int index=0; index<MATSIZE; index++)
			{
				res = res + input_A[row][index]*input_B[index][col];
			}
			output_C[row][col] = res;
		}
	}
}
