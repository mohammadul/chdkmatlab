#include <iostream>
#include <cmath>
#include <mex.h>
#include <stdint.h>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <climits>

using namespace std;

#define DATA_TYPE int64_t
#define IS_REAL_2D_FULL_DOUBLE(P) (!mxIsComplex(P) && \
mxGetNumberOfDimensions(P)==2 && !mxIsSparse(P) && mxIsDouble(P))
#define IS_REAL_SCALAR(P) (IS_REAL_2D_FULL_DOUBLE(P) && mxGetNumberOfElements(P)==1)
#define IS_STRING(P) (mxIsChar(P))
#define IMG_OUT plhs[0]
#define FNAME_IN prhs[0]
#define OUTBITS_IN prhs[1]
#define RAW_SIZE_IN prhs[2]


typedef unsigned (*get_pixel_func_t)(uint8_t *p, unsigned row_bytes, unsigned x, unsigned y);
typedef unsigned (*convert_pixel_func_t)(unsigned value);

typedef struct
{
	unsigned ibpp;
	get_pixel_func_t get_pixel;
} op_def_t;

unsigned get_8_pixel(uint8_t *buf, unsigned row_bytes, unsigned x, unsigned y)
{
	return buf[row_bytes*y+x];
}

unsigned get_10_pixel(uint8_t *p, unsigned row_bytes, unsigned x, unsigned y)
{
	uint8_t* addr = p + y * row_bytes + (x>>3) * 10;
	switch (x&7) {
		case 0: return ((0x3fc&(((unsigned short)addr[1])<<2)) | (addr[0] >> 6));
		case 1: return ((0x3f0&(((unsigned short)addr[0])<<4)) | (addr[3] >> 4));
		case 2: return ((0x3c0&(((unsigned short)addr[3])<<6)) | (addr[2] >> 2));
		case 3: return ((0x300&(((unsigned short)addr[2])<<8)) | (addr[5]));
		case 4: return ((0x3fc&(((unsigned short)addr[4])<<2)) | (addr[7] >> 6));
		case 5: return ((0x3f0&(((unsigned short)addr[7])<<4)) | (addr[6] >> 4));
		case 6: return ((0x3c0&(((unsigned short)addr[6])<<6)) | (addr[9] >> 2));
		case 7: return ((0x300&(((unsigned short)addr[9])<<8)) | (addr[8]));
	}
	return 0;
}

unsigned get_12_pixel(uint8_t *p, unsigned row_bytes, unsigned x, unsigned y)
{
 uint8_t* addr = p + y * row_bytes + (x>>2) * 6;
 switch (x&3) {
  case 0: return ((unsigned short)(addr[1]) << 4) | (addr[0] >> 4);
  case 1: return ((unsigned short)(addr[0] & 0x0F) << 8) | (addr[3]);
  case 2: return ((unsigned short)(addr[2]) << 4) | (addr[5] >> 4);
  case 3: return ((unsigned short)(addr[5] & 0x0F) << 8) | (addr[4]);
 }
 return 0;
}

unsigned get_16_pixel(uint8_t *buf, unsigned row_bytes, unsigned x, unsigned y)
{
	return ((uint16_t *)buf)[(row_bytes/2)*y + x];
}

void swap_bytes(unsigned char *src, unsigned char *dst, size_t size)
{
    unsigned char c1, c2;
	while(size>1)
	{
		c1=*src++;
		c2=*src++;
		*dst++=c2;
		*dst++=c1;
		size-=2;
	}
}

#define OP_DEF(X) {X, get_##X##_pixel},
op_def_t op_defs[] =
{
	OP_DEF(8)
	OP_DEF(10)
	OP_DEF(12)
	OP_DEF(16)
};

#define NUM_OP_DEFS (sizeof(op_defs)/sizeof(op_def_t))

const op_def_t *find_op(unsigned int type)
{
	unsigned int i;
    if(type<NUM_OP_DEFS) return op_defs+type;
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    double *out_im = NULL, *size_in = NULL;
    uint8_t *in_data;
	FILE *fp = NULL;
	char *iname = NULL, errtxt[256];
	const op_def_t *op = NULL;
    unsigned int height = 0, width = 0, npixels, in_row_bytes, i, j, k;
	struct stat st;
	size_t rcount;
	int status;
    mwSize buflen;

    /* check number of arguments */
    /** out = LoadCHDKData(filename, outputbit) **/

    if(nrhs<1 || nrhs>2) mexErrMsgTxt("Wrong number of input arguments.");
    else if(nlhs>1) mexErrMsgTxt("Too many output arguments.");

    /* check all arguments one by one if valid or set default if empty */
    if(nrhs<3)
    {
        height = 2480;
        width = 3336;
    }
    else
    {
        if(mxIsEmpty(RAW_SIZE_IN))
        {
            height = 2480;
            width = 3336;
        }
        else
        {
            if(!IS_REAL_2D_FULL_DOUBLE(RAW_SIZE_IN) || mxGetNumberOfElements(RAW_SIZE_IN)!=2) mexErrMsgTxt("RAW_SIZE must be 2X1 or 1X2 positive full double array.");
            size_in = mxGetPr(RAW_SIZE_IN);
            height = static_cast<int>(size_in[0]);
            width = static_cast<int>(size_in[1]);
            if(height<0 || width<0)mexErrMsgTxt("RAW_SIZE must be 2X1 or 1X2 positive full double array.");
            if((width*op->ibpp)%8 != 0)
            {
                mexWarnMsgTxt("WIDTH is not an integral number of bytes at input BPP.");
            }
        }
    }

    if(nrhs<2) op = find_op(1);
    else
    {
        if(mxIsEmpty(OUTBITS_IN)) op = find_op(1);
        else
        {
            if(!IS_REAL_SCALAR(OUTBITS_IN) || mxGetScalar(OUTBITS_IN)<0 || mxGetScalar(OUTBITS_IN)>3) mexErrMsgTxt("INPUT_BPP must be between 0 and 3.");
            op = find_op(static_cast<int>(mxGetScalar(OUTBITS_IN)));
        }
    }

   /* get pointer to filename */
    if(!IS_STRING(FNAME_IN)) mexErrMsgTxt("FILENAME must be a string.");
    buflen = mxGetN(prhs[0])*sizeof(mxChar)+1;
    iname = (char*)mxCalloc(buflen, sizeof(char));
    status = mxGetString(prhs[0], iname, buflen);
    if(status)
    {
        mxFree(iname);
        mexErrMsgTxt("FILENAME is not valid.");
    }

    if(stat(iname,&st)!= 0)
    {
        mxFree(iname);
        mexErrMsgTxt("Cannot open input file.");
    }

	in_data = (uint8_t*)mxMalloc(st.st_size);
	if(in_data==NULL)
    {
        mxFree(iname);
        mexErrMsgTxt("MALLOC error.");
    }

	fp = fopen(iname,"rb");
	mxFree(iname);

	if(fp==NULL)
	{
        mxFree(in_data);
        mexErrMsgTxt("Cannot open input file.");
    }

	rcount = fread(in_data, 1, st.st_size, fp);
	fclose(fp);
    if(rcount!=st.st_size)
	{
        mxFree(in_data);
        mexErrMsgTxt("Cannot read input file.");
    }

	npixels = height*width;
	if((npixels*op->ibpp)>>3 != st.st_size)
    {
        mxFree(in_data);
        sprintf(errtxt,"Mismatched size ((%u*%u*%u)/8 -> %u) != %u.", width, height, op->ibpp, (npixels*op->ibpp) >> 3, (unsigned)st.st_size);
    	mexErrMsgTxt(errtxt);
	}

	IMG_OUT = mxCreateDoubleMatrix(height, width, mxREAL);
    out_im = mxGetPr(IMG_OUT);
    if(out_im==NULL)
	{
        mxFree(in_data);
        mexErrMsgTxt("MALLOC error.");
    }

    in_row_bytes = (width*op->ibpp)>>3;
    k = 0;
    for(i=0; i<width; i++)
    {
        for(j=0; j<height; j++)
        {
            out_im[k] = op->get_pixel(in_data, in_row_bytes, i, j);
            ++k;
        }
    }
	mxFree(in_data);
}
