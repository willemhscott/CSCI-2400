#include <stdio.h>
#include "cs1300bmp.h"
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include "Filter.h"

using namespace std;

#include "rdtsc.h"

//
// Forward declare the functions
//
Filter *readFilter(string filename);

double applyFilter(Filter *filter, cs1300bmp *input, cs1300bmp *output);

int
main(int argc, char **argv) {

    if (argc < 2) {
        fprintf(stderr, "Usage: %s filter inputfile1 inputfile2 .... \n", argv[0]);
        return 0;
    }

    //
    // Convert to C++ strings to simplify manipulation
    //
    string filtername = argv[1];

    //
    // remove any ".filter" in the filtername
    //
    string filterOutputName = filtername;
    string::size_type loc = filterOutputName.find(".filter");
    if (loc != string::npos) {
        //
        // Remove the ".filter" name, which should occur on all the provided filters
        //
        filterOutputName = filtername.substr(0, loc);
    }

    Filter *filter = readFilter(filtername);

    double sum = 0.0;
    int samples = 0;

    for (int inNum = 2; inNum < argc; inNum++) {
        string inputFilename = argv[inNum];
        string outputFilename = "filtered-" + filterOutputName + "-" + inputFilename;
        struct cs1300bmp *input = new struct cs1300bmp;
        struct cs1300bmp *output = new struct cs1300bmp;
        int ok = cs1300bmp_readfile((char *) inputFilename.c_str(), input);

        if (ok) {
            double sample = applyFilter(filter, input, output);
            sum += sample;
            samples++;
            cs1300bmp_writefile((char *) outputFilename.c_str(), output);
        }
        delete input;
        delete output;
    }
    fprintf(stdout, "Average cycles per sample is %f\n", sum / samples);

}

class Filter *
readFilter(string filename) {
    ifstream input(filename.c_str());

    if (!input.bad()) {
        int size = 0;
        input >> size;
        Filter *filter = new Filter(size);
        int div;
        input >> div;
        filter->setDivisor(div);
        for (int i = 0; i < size; i++) {
            for (int j = 0; j < size; j++) {
                int value;
                input >> value;
                filter->set(i, j, value);
            }
        }
        return filter;
    } else {
        cerr << "Bad input in readFilter:" << filename << endl;
        exit(-1);
    }
}


double applyFilter(class Filter *filter, cs1300bmp * input, cs1300bmp * output) {
    long long cycStart, cycStop;

    cycStart = rdtscll();

    int iw = output->width = input->width;
    iw--; // We need to update output width/height and use a constant width/height that is 1 less
    int ih = output->height = input->height;
    ih--;

    // Size is always 3
    int divisor = filter->getDivisor();
    
        // Add some super cool openmp threaded looping
        // Reordered loops to operate on the same plane first, then same row, then same column
        // Got rid of plane, replaced with 3x of each part since loop size is always 3
        #pragma omp parallel for collapse(2)
        for (int row = 1; row < ih; row++) {
            for (int col = 1; col < iw; col++) {
                int crc1 = output->color[0][row][col] = 0;
                int crc2 = output->color[1][row][col] = 0;
                int crc3 = output->color[2][row][col] = 0;
                // Pull out some simple repeated addition
                int rm1 = row - 1;
                int cm1 = col - 1;

                /*
                // Reordered loops to operate on the same row, then same column
                for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 3; j++) {
                        // Pulled out common exp
                        int ri = rm1 + i;
                        int rj = cm1 + j;
                        int fspot = filter->get(i, j);
                        crc1 += (input->color[0][ri][rj] * fspot);
                        crc2 += (input->color[1][ri][rj] * fspot);
                        crc3 += (input->color[2][ri][rj] * fspot);
                    }
                }
                */
                
                for (int i = 0; i < 3; i++) {
                        // Pulled out common exp
                        int ri = rm1 + i;
                        int rj = cm1;
                        int fspot = filter->get(i, 0);
                        crc1 += (input->color[0][ri][rj] * fspot);
                        crc2 += (input->color[1][ri][rj] * fspot);
                        crc3 += (input->color[2][ri][rj] * fspot);
                        
                        rj++;
                        fspot = filter->get(i, 1);
                        crc1 += (input->color[0][ri][rj] * fspot);
                        crc2 += (input->color[1][ri][rj] * fspot);
                        crc3 += (input->color[2][ri][rj] * fspot);
                        
                        rj++;
                        fspot = filter->get(i, 2);
                        crc1 += (input->color[0][ri][rj] * fspot);
                        crc2 += (input->color[1][ri][rj] * fspot);
                        crc3 += (input->color[2][ri][rj] * fspot);
                }
                
                
                output->color[0][row][col] = crc1;
                output->color[1][row][col] = crc2;
                output->color[2][row][col] = crc3;

                if (divisor != 1) { // Ignore 1 divisor
                    output->color[0][row][col] /= divisor;
                    output->color[1][row][col] /= divisor;
                    output->color[2][row][col] /= divisor;
                }

                if (output->color[0][row][col] < 0) {
                    output->color[0][row][col] = 0;
                } else if (output->color[0][row][col] > 255) {
                    output->color[0][row][col] = 255;
                }
                
                if (output->color[1][row][col] < 0) {
                    output->color[1][row][col] = 0;
                } else if (output->color[1][row][col] > 255) {
                    output->color[1][row][col] = 255;
                }
                
                if (output->color[2][row][col] < 0) {
                    output->color[2][row][col] = 0;
                } else if (output->color[2][row][col] > 255) {
                    output->color[2][row][col] = 255;
                }
            }
    }

    cycStop = rdtscll();
    double diff = cycStop - cycStart;
    double diffPerPixel = diff / (output->width * output->height);
    fprintf(stderr, "Took %f cycles to process, or %f cycles per pixel\n",
            diff, diff / (output->width * output->height));
    return diffPerPixel;
}

