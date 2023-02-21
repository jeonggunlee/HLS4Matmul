# HLS4Matmul

2023.02.21

## HLS code for simple unoptimized matrix multiplications (Initial Try for Learning HLS)
- Jeong-Gun Lee / Hallym University

![MM Accelerator on Zynq FPGA](./MM_Blocks.png)

* Note

1. Zynq Processor Configuration: Keep in mind that we have to enable the use of HP ports for utilizing DMA.
2. DMA transfer data are typed as u32. Sometimes, we need to special functions to translate u32 to/from float.
