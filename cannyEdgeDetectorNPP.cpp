/* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
#define WINDOWS_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#pragma warning(disable : 4819)
#endif

#include <Exceptions.h>
#include <ImageIO.h>
#include <ImagesCPU.h>
#include <ImagesNPP.h>
#include <cuda_runtime.h>
#include <fstream>
#include <helper_cuda.h>
#include <helper_string.h>
#include <iostream>
#include <npp.h>
#include <string.h>

inline int cudaDeviceInit(int argc, const char **argv)
{
    int deviceCount;
    checkCudaErrors(cudaGetDeviceCount(&deviceCount));

    if (deviceCount == 0)
    {
        std::cerr << "CUDA error: no devices supporting CUDA." << std::endl;
        exit(EXIT_FAILURE);
    }

    int dev = findCudaDevice(argc, argv);

    cudaDeviceProp deviceProp;
    cudaGetDeviceProperties(&deviceProp, dev);
    std::cerr << "cudaSetDevice GPU" << dev << " = " << deviceProp.name << std::endl;

    checkCudaErrors(cudaSetDevice(dev));

    return dev;
}

int main(int argc, char *argv[])
{
    printf("%s Starting...\n\n", argv[0]);

    try
    {
        std::string sFilename;
        char *filePath;

        cudaDeviceInit(argc, (const char **)argv);

        NppStreamContext nppStreamCtx;
        nppStreamCtx.hStream =
            0; // The NULL stream by default, set this to whatever your stream ID is if not the NULL stream.

        cudaError_t cudaError = cudaGetDevice(&nppStreamCtx.nCudaDeviceId);
        if (cudaError != cudaSuccess)
        {
            printf("CUDA error: no devices supporting CUDA.\n");
            return NPP_NOT_SUFFICIENT_COMPUTE_CAPABILITY;
        }

        const NppLibraryVersion *libVer = nppGetLibVersion();

        printf("NPP Library Version %d.%d.%d\n", libVer->major, libVer->minor, libVer->build);

        int driverVersion, runtimeVersion;
        cudaDriverGetVersion(&driverVersion);
        cudaRuntimeGetVersion(&runtimeVersion);

        printf("CUDA Driver  Version: %d.%d\n", driverVersion / 1000, (driverVersion % 100) / 10);
        printf("CUDA Runtime Version: %d.%d\n\n", runtimeVersion / 1000, (runtimeVersion % 100) / 10);

        cudaError = cudaDeviceGetAttribute(&nppStreamCtx.nCudaDevAttrComputeCapabilityMajor,
                                           cudaDevAttrComputeCapabilityMajor,
                                           nppStreamCtx.nCudaDeviceId);
        if (cudaError != cudaSuccess)
            return NPP_NOT_SUFFICIENT_COMPUTE_CAPABILITY;

        cudaError = cudaDeviceGetAttribute(&nppStreamCtx.nCudaDevAttrComputeCapabilityMinor,
                                           cudaDevAttrComputeCapabilityMinor,
                                           nppStreamCtx.nCudaDeviceId);
        if (cudaError != cudaSuccess)
            return NPP_NOT_SUFFICIENT_COMPUTE_CAPABILITY;

        cudaError = cudaStreamGetFlags(nppStreamCtx.hStream, &nppStreamCtx.nStreamFlags);

        cudaDeviceProp oDeviceProperties;

        cudaError = cudaGetDeviceProperties(&oDeviceProperties, nppStreamCtx.nCudaDeviceId);

        nppStreamCtx.nMultiProcessorCount = oDeviceProperties.multiProcessorCount;
        nppStreamCtx.nMaxThreadsPerMultiProcessor = oDeviceProperties.maxThreadsPerMultiProcessor;
        nppStreamCtx.nMaxThreadsPerBlock = oDeviceProperties.maxThreadsPerBlock;
        nppStreamCtx.nSharedMemPerBlock = oDeviceProperties.sharedMemPerBlock;

        if (checkCmdLineFlag(argc, (const char **)argv, "input"))
        {
            getCmdLineArgumentString(argc, (const char **)argv, "input", &filePath);
        }
        else
        {
            filePath = sdkFindFilePath("Lena.pgm", argv[0]);
        }

        if (filePath)
        {
            sFilename = filePath;
        }
        else
        {
            sFilename = "Lena.pgm";
        }

        // if we specify the filename at the command line, then we only test
        // sFilename[0].
        int file_errors = 0;
        std::ifstream infile(sFilename.data(), std::ifstream::in);

        if (infile.good())
        {
            std::cout << "cannyEdgeDetectionNPP opened: <" << sFilename.data() << "> successfully!" << std::endl;
            file_errors = 0;
            infile.close();
        }
        else
        {
            std::cout << "cannyEdgeDetectionNPP unable to open: <" << sFilename.data() << ">" << std::endl;
            file_errors++;
            infile.close();
        }

        if (file_errors > 0)
        {
            exit(EXIT_FAILURE);
        }

        std::string sResultFilename = sFilename;

        std::string::size_type dot = sResultFilename.rfind('.');

        if (dot != std::string::npos)
        {
            sResultFilename = sResultFilename.substr(0, dot);
        }

        sResultFilename += "_cannyEdgeDetection.pgm";

        if (checkCmdLineFlag(argc, (const char **)argv, "output"))
        {
            char *outputFilePath;
            getCmdLineArgumentString(argc, (const char **)argv, "output", &outputFilePath);
            sResultFilename = outputFilePath;
        }

        // declare a host image object for an 8-bit grayscale image
        npp::ImageCPU_8u_C1 oHostSrc;
        // load gray-scale image from disk
        npp::loadImage(sFilename, oHostSrc);
        // declare a device image and copy construct from the host image,
        // i.e. upload host to device
        npp::ImageNPP_8u_C1 oDeviceSrc(oHostSrc);

        NppiSize oSrcSize = {(int)oDeviceSrc.width(), (int)oDeviceSrc.height()};
        NppiPoint oSrcOffset = {0, 0};

        // create struct with ROI size
        NppiSize oSizeROI = {(int)oDeviceSrc.width(), (int)oDeviceSrc.height()};
        // allocate device image of appropriately reduced size
        npp::ImageNPP_8u_C1 oDeviceDst(oSizeROI.width, oSizeROI.height);

        int nBufferSize = 0;
        Npp8u *pScratchBufferNPP = 0;

        // get necessary scratch buffer size and allocate that much device memory
        NPP_CHECK_NPP(nppiFilterCannyBorderGetBufferSize(oSizeROI, &nBufferSize));

        cudaMalloc((void **)&pScratchBufferNPP, nBufferSize);

        Npp16s nLowThreshold = 72;
        Npp16s nHighThreshold = 256;

        if ((nBufferSize > 0) && (pScratchBufferNPP != 0))
        {
            NPP_CHECK_NPP(nppiFilterCannyBorder_8u_C1R_Ctx(oDeviceSrc.data(),
                                                           oDeviceSrc.pitch(),
                                                           oSrcSize,
                                                           oSrcOffset,
                                                           oDeviceDst.data(),
                                                           oDeviceDst.pitch(),
                                                           oSizeROI,
                                                           NPP_FILTER_SOBEL,
                                                           NPP_MASK_SIZE_3_X_3,
                                                           nLowThreshold,
                                                           nHighThreshold,
                                                           nppiNormL2,
                                                           NPP_BORDER_REPLICATE,
                                                           pScratchBufferNPP,
                                                           nppStreamCtx));
        }

        // free scratch buffer memory
        cudaFree(pScratchBufferNPP);

        // declare a host image for the result
        npp::ImageCPU_8u_C1 oHostDst(oDeviceDst.size());
        // and copy the device result data into it
        oDeviceDst.copyTo(oHostDst.data(), oHostDst.pitch());

        saveImage(sResultFilename, oHostDst);
        std::cout << "Saved image: " << sResultFilename << std::endl;

        nppiFree(oDeviceSrc.data());
        nppiFree(oDeviceDst.data());

        exit(EXIT_SUCCESS);
    }
    catch (npp::Exception &rException)
    {
        std::cerr << "Program error! The following exception occurred: \n";
        std::cerr << rException << std::endl;
        std::cerr << "Aborting." << std::endl;

        exit(EXIT_FAILURE);
    }
    catch (...)
    {
        std::cerr << "Program error! An unknow type of exception occurred. \n";
        std::cerr << "Aborting." << std::endl;

        exit(EXIT_FAILURE);
        return -1;
    }

    return 0;
}