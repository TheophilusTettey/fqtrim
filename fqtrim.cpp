#define VERSION "0.9.6"
#include "GArgs.h"
#include "GStr.h"
#include "GHash.hh"
#include "GList.hh"
#include <ctype.h>
#include "GAlnExtend.h"
#ifndef NOTHREADS
#include "GThreads.h"
#endif

#include "time.h"
#include "sys/time.h"

//DEBUG ONLY: uncomment this to show trimming progress
//#define TRIMDEBUG 1

#define USAGE "fqtrim v" VERSION ". Usage:\n\
fqtrim [{-5 <5adapter> -3 <3adapter>|-f <adapters_file>}] [-a <min_match>]\\\n\
   [-R] [-q <minq> [-t <trim_max_len>]] [-p <numcpus>] [-P {64|33}] \\\n\
   [-m <max_percN>] [--ntrimdist=<max_Ntrim_dist>] [-l <minlen>] [-C]\\\n\
   [-o <outsuffix> [--outdir <outdir>]] [-D][-Q][-O] [-n <rename_prefix>]\\\n\
   [-r <trim_report.txt>] [-y <min_poly>] [-A|-B] <input.fq>[,<input_mates.fq>\\\n\
 \n\
 Trim low quality bases at the 3' end and can trim adapter sequence(s), filter\n\
 for low complexity and collapse duplicate reads.\n\
 If read pairs should be trimmed and kept together (i.e. never discarding\n\
 only one read in a pair), the two file names should be given delimited by a comma\n\
 or a colon character.\n\
\n\
Options:\n\
-n  rename the reads using the <prefix> followed by a read counter;\n\
    if -C option was also provided, the suffix \"_x<N>\" is appended\n\
    (where <N> is the read duplication count)\n\
-o  write the trimmed/filtered reads to file(s) named <input>.<outsuffix>\n\
    which will be created in the current (working) directory (unless --outdir\n\
    is used); this suffix should include the file extension; if this extension\n\
    is .gz, .gzip or .bz2 then the output will be compressed accordingly.\n\
    NOTE: if the input file is '-' (stdin) then this is the full name of the\n\
    output file, not just the suffix.\n\
--outdir for -o option, write the output file(s) to <outdir> directory instead\n\
-f  file with adapter sequences to trim, each line having this format:\n\
    [<5_adapter_sequence>][ <3_adapter_sequence>]\n\
-5  trim the given adapter or primer sequence at the 5' end of each read\n\
    (e.g. -5 CGACAGGTTCAGAGTTCTACAGTCCGACGATC)\n\
-3  trim the given adapter sequence at the 3' end of each read\n\
    (e.g. -3 TCGTATGCCGTCTTCTGCTTG)\n\
-A  disable polyA/T trimming (enabled by default)\n\
-B  trim polyA/T at both ends (default: only poly-A at 3' end, poly-T at 5')\n\
-O  only reads affected by trimming will be printed\n\
-y  minimum length of poly-A/T run to remove (6)\n\
-q  trim read ends where the quality value drops below <minq>\n\
-w  for -q, sliding window size for calculating avg. quality (default 6)\n\
-t  for -q, limit maximum trimming at either end to <trim_max_len>\n\
-m  maximum percentage of Ns allowed in a read after trimming (default 5)\n\
-l  minimum read length after trimming (if the remaining sequence is shorter\n\
    than this, the read will be discarded (trashed)(default: 16)\n\
-r  write a \"trimming report\" file listing the affected reads with a list\n\
    of trimming operations\n\
--aidx option can only be given with -r and -f options and it makes all the \n\
	vector/adapter trimming operations encoded as a,b,c,.. instead of V,\n\
	corresponding to the order of adapter sequences in the -f file\n\
-T  write the number of bases trimmed at 5' and 3' ends after the read names\n\
    in the FASTA/FASTQ output file(s)\n\
-D  pass reads through a low-complexity (dust) filter and discard any read\n\
    that has over 50% of its length masked as low complexity\n\
--dmask option is the same with -D but fqtrim will actually mask the low \n\
    complexity regions with Ns in the output sequence\n\
    of low-complexity sequence detected in the reads\n\
-C  collapse duplicate reads and append a _x<N>count suffix to the read\n\
    name (where <N> is the duplication count)\n\
-p  use <numcpus> CPUs (threads) on the local machine\n\
-P  input is phred64/phred33 (use -P64 or -P33)\n\
-Q  convert quality values to the other Phred qv type\n\
-M  disable read name consistency check for paired reads\n\
-V  show verbose trimming summary\n\
Advanced adapter/primer match options (for -f or -5 , -3 options):\n\
  -a      minimum length of exact suffix-prefix match with adapter sequence that\n\
          can be trimmed at either end of the read (default: 6)\n\
  --pid5  minimum percent identity for adapter match at 5' end (default 96.0)\n\
  --pid3  minimum percent identity for adapter match at 3' end (default 94.0)\n\
  --mism  mismatch penalty for scoring the adapter alignment (default 3)\n\
  --match match reward for scoring the adapter alignment (default 1)\n\
  -R      also look for terminal alignments with the reverse complement\n\
          of the adapter sequence(s)\n\
 "
/*
  --mdist maximum distance from the ends of the read for an adapter\n\
          alignment to be considered for trimming; can be given as\n\
          a percentage of read length if followed by '%' (default: \n\
*/
// example 3' adapter for miRNAs: TCGTATGCCGTCTTCTGCTTG

//For paired reads sequencing:
//3' : ACACTCTTTCCCTACACGACGCTCTTCCGATCT
//5' : GATCGGAAGAGCGGTTCAGCAGGAATGCCGAG
//FILE* f_out=NULL; //stdout if not provided
//FILE* f_out2=NULL; //for paired reads
//FILE* f_in=NULL; //input fastq (stdin if not provided)
//FILE* f_in2=NULL; //for paired reads

FILE* freport=NULL;

bool debug=false;
bool verbose=false;
bool doCollapse=false;
bool doDust=false;
bool doPolyTrim=true;
bool fastaOutput=false;
bool trimReport=false; //create a trim/trash report file
bool showAdapterIdx=false;
bool trimInfo=false; //trim info added to the output reads
bool polyBothEnds=false; //attempt poly-A/T trimming at both ends
bool onlyTrimmed=false; //report only trimmed reads
bool show_Trim=false;
bool dustMask=false;
bool pairedOutput=false;
bool revCompl=false; //also reverse complement adapter sequences
bool disableMateNameCheck=false;
int adapter_idx=0;
int min_read_len=16;
int num_cpus=1; // -p option
int readBufSize=200; //how many reads to fetch at a time (useful for multi-threading)

double max_perc_N=5.0;
double perc_lenN=12.0; // incremental distance from ends, in percentage of read length
          // where N-trimming is allowed (default:12 %) (autolimited to 20)
int dist_lenN=0; // incremental distance from either end (in bp) 
          // where N-trimming is allowed (default: none, perc_lenN controls it)

int dust_cutoff=16;
bool isfasta=false;
bool convert_phred=false;
GStr outdir(".");
GStr outsuffix; // -o
GStr prefix;
GStr zcmd;
char isACGT[256];

uint inCounter=0;
uint outCounter=0;

int gtrash_s=0;
int gtrash_poly=0;
int gtrash_Q=0;
int gtrash_N=0;
int gtrash_D=0;
int gtrash_V=0;
int gtrash_X=0;
uint gnum_trimN=0; //reads trimmed by N%
uint gnum_trimQ=0; //reads trimmed by qv threshold
uint gnum_trimV=0; //reads trimmed by adapter match
uint gnum_trimA=0; //reads trimmed by polyA
uint gnum_trimT=0; //reads trimmed by polyT
uint gnum_trim5=0; //number of reads trimmed at 5' end
uint gnum_trim3=0; //number of reads trimmed at 3' end

uint64 gb_totalIn=0; //total number of input bases
uint64 gb_totalN=0;  //total number of undetermined bases found in input bases

uint64 gb_trimN=0; //total number of bases trimmed due to N-trim
uint64 gb_trimQ=0; //number of bases trimmed due to qv threshold
uint64 gb_trimV=0; //total number of bases trimmed due to adapter matches
uint64 gb_trimA=0; //number of bases trimmed due to poly-A tails
uint64 gb_trimT=0; //number of bases trimmed due to poly-T tails
uint64 gb_trim5=0; //total bases trimmed on the 5' side
uint64 gb_trim3=0; //total bases trimmed on the 3' side
//int min_trimmed5=INT_MAX;
//int min_trimmed3=INT_MAX;

int qvtrim_qmin=0;
int qvtrim_max=0;  //(-t) for -q, do not trim the 3'-end more than this number of bases
int qvtrim_win=6;  //(-w) for -q, sliding window length for avg qual calculation
int qv_phredtype=0; // could be 64 or 33 (0 means undetermined yet)
int qv_cvtadd=0; //could be -31 or +31

// adapter matching metrics -- for X-drop ungapped extension
//const int match_reward=2;
//const int mismatch_penalty=3;
int match_reward=1;
int mismatch_penalty=3;
int Xdrop=8;
int minEndAdapter=6;
//adapter matching percent identiy thresholds:
double min_pid3=94.0; //min % identity for primer/adapter match at 3' end
double min_pid5=96.0; //min % identity for primer/adapter match at 5' end


const int poly_m_score=2; //match score for poly-A/T extension
const int poly_mis_score=-3; //mismatch for poly-A/T extension
const int poly_dropoff_score=7;
int poly_minScore=12; //i.e. an exact match of 6 bases at the proper ends WILL be trimmed

const char *polyA_seed="AAAA";
const char *polyT_seed="TTTT";


#ifndef NOTHREADS

GFastMutex readMutex; //reading input
GFastMutex writeMutex; //writing the output reads
GFastMutex reportMutex; //trim report writing
GFastMutex statsMutex; //for updating global stats
void workerThread(GThreadData& td); // Thread function

#endif



struct STrimOp {
	byte tend; //5 or 3
	char tcode; //'N','A','T','V' or 'a'..'z'
	short tlen; //trim length
	STrimOp(byte e=0, char c=0, short l=0) {
		assign(e,c,l);
	}
	void assign(byte e,char c, short l) {
		tend=e;
		tcode=c;
		tlen=l;
	}
};

struct RData {
	GStr seq;
	GStr qv;
	GStr rid;
	GStr rinfo;
	GVec<STrimOp> trimhist;
	int trim5;
	int trim3;
	char trashcode;
	int l3() { return seq.length()-trim3-1; }
	RData():seq(),qv(),rid(),rinfo(), trimhist(), trim5(0), trim3(0), trashcode(0) {}
	GStr getTrimSeq() {
		if (trim5 || trim3)
			return seq.substr(trim5, seq.length()-trim5-trim3);
			else return seq;
	}
	GStr getTrimQv() {
		if (trim5 || trim3)
			return qv.substr(trim5, qv.length()-trim5-trim3);
		else return qv;
	}

	void clear() { seq="";qv="";rid="";rinfo=""; trimhist.Clear();
	               trim5=0; trim3=0; trashcode=0; }
};

struct RInfo {
	GLineReader* fq;
	GLineReader* fq2;
	FILE* f_out;
	FILE* f_out2;
	GStr infname;
	GStr infname2;

	RInfo(FILE* fo=NULL, FILE* fo2=NULL, GLineReader* fl=NULL,
			GLineReader* fl2=NULL): fq(fl), fq2(fl2),
			f_out(fo), f_out2(fo2), infname(), infname2() { }
};



struct CASeqData {
	//positional data for every possible hexamer in an adapter
	GVec<uint16>* pz[4096]; //0-based coordinates of all possible hexamers in the adapter sequence
	GVec<uint16>* pzr[4096]; //0-based coordinates of all possible hexamers for the reverse complement of the adapter sequence
	GStr seq; //actual adapter sequence data
	GStr seqr; //reverse complement sequence
	int fidx; //index of adapter in the file (order they are given)
	int amlen; //fraction of adapter length matching that's enough to consider the alignment
	GAlnTrimType trim_type;
	bool use_reverse;
	CASeqData(bool rev=false, int aidx=0):seq(),seqr(),
			fidx(aidx), amlen(0), use_reverse(rev) {
		trim_type=galn_None; //should be updated later!
		for (int i=0;i<4096;i++) {
			pz[i]=NULL;
			pzr[i]=NULL;
		}
	}

	void update(const char* s) {
		seq=s;
		table6mers(seq.chars(), seq.length(), pz);
		amlen=calc_safelen(seq.length());
		if (!use_reverse) return;
		//reverse complement
		seqr=s;
		int slen=seq.length();
		for (int i=0;i<slen;i++)
			seqr[i]=ntComplement(seq[slen-i-1]);
		table6mers(seqr.chars(), seqr.length(), pzr);
	}

	void freePosData() {
		for (int i=0;i<4096;i++) {
			delete pz[i];
			delete pzr[i];
		}
	}
	~CASeqData() { freePosData(); }
};

GPVec<CASeqData> adapters5(false);
GPVec<CASeqData> adapters3(false);
GPVec<CASeqData> all_adapters(true);

// element in dhash:
class FqDupRec {
 public:
   int count; //how many of these reads are the same
   int len; //length of qv
   char* firstname; //optional, only if we want to keep the original read names
   char* qv;
   FqDupRec(GStr* qstr=NULL, const char* rname=NULL) {
     len=0;
     qv=NULL;
     firstname=NULL;
     count=0;
     if (qstr!=NULL) {
       qv=Gstrdup(qstr->chars());
       len=qstr->length();
       count++;
       }
     if (rname!=NULL) firstname=Gstrdup(rname);
     }
   ~FqDupRec() {
     GFREE(qv);
     GFREE(firstname);
     }
   void add(GStr& d) { //collapse another record into this one
     if (d.length()!=len)
       GError("Error at FqDupRec::add(): cannot collapse reads with different length!\n");
     count++;
     for (int i=0;i<len;i++)
       qv[i]+=(d[i]-qv[i])/count; //the mean is calculated incrementally
     }
 };

struct CTrimHandler {
	CGreedyAlignData* gxmem_l;
	CGreedyAlignData* gxmem_r;
	GVec<RData> rbuf;   //read buffer
	int rbuf_p; //index of next read unprocessed from the reading buffer
	GVec<RData> rbuf2;  //mate read buffer
	int rbuf2_p;
	RInfo* rinfo;
	int incounter;
	int outcounter;
	int trash_s;
	int trash_poly;
	int trash_Q;
	int trash_N;
	int trash_D;
	int trash_V;
	int trash_X;
	uint num_trimN, num_trimQ, num_trimV,
	  num_trimA, num_trimT, num_trim5, num_trim3;

	uint64 b_totalIn, b_totalN, b_trimN, b_trimQ,
	  b_trimV, b_trimA, b_trimT, b_trim5, b_trim3;

	CTrimHandler(RInfo* ri=NULL): gxmem_l(NULL), gxmem_r(NULL), rbuf(readBufSize), rbuf_p(-1),
			rbuf2(0),rbuf2_p(-1), rinfo(ri), incounter(0), outcounter(0),trash_s(0), trash_poly(0),
			trash_Q(0), trash_N(0), trash_D(0), trash_V(0),
			trash_X(0),
			num_trimN(0), num_trimQ(0), num_trimV(0), num_trimA(0), num_trimT(0), num_trim5(0), num_trim3(0),
			b_totalIn(0), b_totalN(0), b_trimN(0), b_trimQ(0), b_trimV(0),
			b_trimA(0), b_trimT(0), b_trim5(0), b_trim3(0) {
      if (adapters5.Count()>0)
        gxmem_l=new CGreedyAlignData(match_reward, mismatch_penalty, Xdrop);
      if (adapters3.Count()>0)
        gxmem_r=new CGreedyAlignData(match_reward, mismatch_penalty, Xdrop);
      if (ri && ri->fq2) {
    	  rbuf2.setCapacity(readBufSize);
      }
	}
	void updateTrashCounts(RData& rd);

	void Clear() {
		 rbuf_p=0; rbuf2_p=0;
		 incounter=0; outcounter=0;
		 rbuf.Clear(); rbuf2.Clear();
		 trash_s=0; trash_poly=0;
		 trash_Q=0; trash_N=0;
		 trash_X=0;
		 trash_D=0; num_trimV=0;
		 num_trimN=0;num_trimQ=0;
		 num_trimA=0;num_trimT=0;
		 num_trim5=0;num_trim3=0;
		 b_totalIn=0;b_totalN=0;
		 b_trimN=0;b_trimQ=0;
		 b_trimV=0;b_trimA=0;b_trimT=0;
		 b_trim5=0;b_trim3=0;
	}

	void updateCounts() {
#ifndef NOTHREADS
 GLockGuard<GFastMutex> guard(statsMutex);
#endif
	  inCounter+=incounter;
	  outCounter+=outcounter;
	  gtrash_s+=trash_s;
	  gtrash_poly+=trash_poly;
	  gtrash_Q+=trash_Q;
	  gtrash_N+=trash_N;
	  gtrash_D+=trash_D;
	  gtrash_V+=trash_V;
	  gtrash_X+=trash_X;

	  gnum_trimN+=num_trimN;
	  gnum_trimQ+=num_trimQ;
	  gnum_trimV+=num_trimV;
	  gnum_trimA+=num_trimA;
	  gnum_trimT+=num_trimT;
	  gnum_trim5+=num_trim5;
	  gnum_trim3+=num_trim3;
	  gb_totalIn+=b_totalIn;
	  gb_totalN+=b_totalN;
	  gb_trimN+=b_trimN;
	  gb_trimQ+=b_trimQ;
	  gb_trimV+=b_trimV;
	  gb_trimA+=b_trimA;
	  gb_trimT+=b_trimT;
	  gb_trim5+=b_trim5;
	  gb_trim3+=b_trim3;
	}

	~CTrimHandler() {
		delete gxmem_l;
		delete gxmem_r;
	}
	void processAll();
    bool fetchReads();
	void flushReads();

    void writeRead(RData& rd, RData& rd2);
	bool nextRead(RData* & rdata, RData* & rdata2);
	bool processRead();

	char process_read(RData& r);
	//returns 0 if the read was untouched, 1 if it was trimmed and a trash code if it was trashed
	//void trim_report(char trashcode, GStr& rname, GVec<STrimOp>& t_hist, FILE* freport);
	void trim_report(RData& rd, int mate=0);

	bool ntrim(GStr& rseq, int &l5, int &l3, double& pN); //returns true if any trimming occured
	bool qtrim(GStr& qvs, int &l5, int &l3); //return true if any trimming occured
	bool trim_poly5(GStr &seq, int &l5, int &l3, const char* poly_seed); //returns true if any trimming occured
	bool trim_poly3(GStr &seq, int &l5, int &l3, const char* poly_seed);
	bool trim_adapter5(GStr& seq, int &l5, int &l3, int &aidx); //returns true if any trimming occured
	bool trim_adapter3(GStr& seq, int &l5, int &l3, int &aidx);
};

//bool getBufRead(GVec<RData>& rbuf, int& rbuf_p, GLineReader* fq, GStr& infname, RData& rdata);


int dust(GStr& seq);

void openfw(FILE* &f, GArgs& args, char opt) {
  GStr s=args.getOpt(opt);
  if (!s.is_empty()) {
      if (s=='-') f=stdout;
      else {
       f=fopen(s.chars(),"w");
       if (f==NULL) GError("Error creating file: %s\n", s.chars());
       }
     }
}

#define FWCLOSE(fh) if (fh!=NULL && fh!=stdout) fclose(fh)
#define FRCLOSE(fh) if (fh!=NULL && fh!=stdin) fclose(fh)

GHash<FqDupRec> dhash; //hash to keep track of duplicates

void addAdapter(GPVec<CASeqData>& adapters, GStr& seq, GAlnTrimType trim_type);
int loadAdapters(const char* fname);

void setupFiles(FILE*& f_in, FILE*& f_in2, FILE*& f_out, FILE*& f_out2,
                       GStr& s, GStr& infname, GStr& infname2);
// uses outsuffix to generate output file names and open file handles as needed

void convertPhred(char* q, int len);
void convertPhred(GStr& q);

int main(int argc, char * const argv[]) {
  GArgs args(argc, argv, "pid5=pid3=mism=ntrimdist=match=XDROP=outdir=dmask;aidx;showtrim;YQDCRVABOTMl:d:3:5:m:n:r:p:P:q:f:w:t:o:z:a:y:");
  int e;
  if ((e=args.isError())>0) {
      GMessage("%s\nInvalid argument: %s\n", USAGE, argv[e]);
      exit(224);
      }
  debug=(args.getOpt('Y')!=NULL);
  verbose=(args.getOpt('V')!=NULL);
  convert_phred=(args.getOpt('Q')!=NULL);
  doCollapse=(args.getOpt('C')!=NULL);
  doDust=(args.getOpt('D')!=NULL);
  revCompl=(args.getOpt('R')!=NULL);
  polyBothEnds=(args.getOpt('B')!=NULL);
  onlyTrimmed=(args.getOpt('O')!=NULL);
  show_Trim=(args.getOpt("showtrim")!=NULL);
  dustMask=(args.getOpt("dmask")!=NULL);
  if (dustMask) doDust=true;
  disableMateNameCheck=(args.getOpt('M')!=NULL);
  if (args.getOpt('A')) doPolyTrim=false;
  /*
  rawFormat=(args.getOpt('R')!=NULL);
  if (rawFormat) {
    GError("Sorry, raw qseq format parsing is not implemented yet!\n");
    }
  */
  prefix=args.getOpt('n');
  GStr s=args.getOpt('l');
  if (!s.is_empty())
     min_read_len=s.asInt();
  s=args.getOpt('m');
  if (!s.is_empty())
     max_perc_N=s.asDouble();
  s=args.getOpt('d');
  if (!s.is_empty()) {
     dust_cutoff=s.asInt();
     doDust=true;
     }
  s=args.getOpt('q');
  if (!s.is_empty()) {
     qvtrim_qmin=s.asInt();
     }
  s=args.getOpt('w');
  if (!s.is_empty()) {
     qvtrim_win=s.asInt(); //must be >0 !
     if (qvtrim_win<1) qvtrim_win=1;
     }
  s=args.getOpt('t');
  if (!s.is_empty()) {
     qvtrim_max=s.asInt();
     }
  s=args.getOpt("match");
  if (!s.is_empty())
	    match_reward=s.asInt();
  s=args.getOpt("ntrimdist");
  if (!s.is_empty()) {
	  dist_lenN=s.asInt();
      if (dist_lenN<=0) GError("Error: invalid --ntrimdist value, must be >0\n");
  }
  s=args.getOpt("mism");
  if (!s.is_empty()) {
	    mismatch_penalty=s.asInt();
        if (mismatch_penalty<0) 
            mismatch_penalty=-mismatch_penalty;
        }
  s=args.getOpt("pid5");
  if (!s.is_empty()) {
     min_pid5=s.asReal();
     //if (min_pid5<50 || min_pid5>100.0) GError();
  }
  s=args.getOpt("pid3");
  if (!s.is_empty()) {
     min_pid3=s.asReal();
  }
  s=args.getOpt("XDROP");
  if (!s.is_empty())
	    Xdrop=s.asInt();
  s=args.getOpt('p');
  if (!s.is_empty()) {
  	num_cpus=s.asInt();
  	if (doCollapse) {
  		GMessage("Warning: -p option ignored (not supported with -C).\n");
  		num_cpus=1;
  	} else
  	if (num_cpus<1) {
  		GMessage("Warning: invalid number of threads specified (-p option).\n");
  		num_cpus=1;
  	}
  }
  s=args.getOpt('P');
  if (!s.is_empty()) {
     int v=s.asInt();
     if (v==33) {
        qv_phredtype=33;
        qv_cvtadd=31;
        }
      else if (v==64) {
        qv_phredtype=64;
        qv_cvtadd=-31;
        }
       else
         GMessage("%s\nInvalid value for -P option (can only be 64 or 33)!\n",USAGE);
     }
  memset((void*)isACGT, 0, 256);
  isACGT['A']=isACGT['a']=isACGT['C']=isACGT['c']=1;
  isACGT['G']=isACGT['g']=isACGT['T']=isACGT['t']=1;
  s=args.getOpt('f');
  if (!s.is_empty()) {
   loadAdapters(s.chars());
   }
  bool fileAdapters=adapters5.Count()+adapters3.Count();
  s=args.getOpt('5');
  if (!s.is_empty()) {
    if (fileAdapters)
      GError("Error: options -5 and -f cannot be used together!\n");
    s.upper();
    addAdapter(adapters5, s, galn_TrimLeft);
    }
  s=args.getOpt('3');
  if (!s.is_empty()) {
    if (fileAdapters)
      GError("Error: options -3 and -f cannot be used together!\n");
    s.upper();
    addAdapter(adapters3, s, galn_TrimRight);
  }
  s=args.getOpt('y');
  if (!s.is_empty()) {
     int minmatch=s.asInt();
     if (minmatch>2)
        poly_minScore=minmatch*poly_m_score;
     else GMessage("Warning: invalid -y option, ignored.\n");
     }
  s=args.getOpt('a');
  if (!s.is_empty()) {
     int minmatch=s.asInt();
     if (minmatch>2)
        minEndAdapter=minmatch;
     else GMessage("Warning: invalid -a option, ignored.\n");
     }


  if (args.getOpt('o')!=NULL) outsuffix=args.getOpt('o');
                         else outsuffix="-";
  if (args.getOpt("outdir")!=NULL) {
    outdir=args.getOpt("outdir");
    if (outdir.length()==0) outdir=".";
    outdir.chomp("/");
    }
  
  trimReport =  (args.getOpt('r')!=NULL);
  trimInfo = (args.getOpt('T')!=NULL);
  if (args.getOpt("aidx")!=NULL) {
	  if (!trimReport || !fileAdapters)
		  GError("Error: option --aidx requires -f and -r options.\n");
	  showAdapterIdx=true;
  }

  int fcount=args.startNonOpt();
  if (fcount==0) {
    GMessage(USAGE);
    exit(224);
    }
   if (fcount>1 && doCollapse) {
    GError("%s Sorry, the -C option only works with a single input file.\n", USAGE);
    }
  if (verbose) args.printCmdLine(stderr);
  if (trimReport)
    openfw(freport, args, 'r');
  char* infile=NULL;

  while ((infile=args.nextNonOpt())!=NULL) {
    //for each input file
    inCounter=0; //counter for input reads
    outCounter=0; //counter for output reads
    gtrash_s=0; //too short from the get go
    gtrash_Q=0;
    gtrash_N=0;
    gtrash_D=0;
    gtrash_poly=0;
    gtrash_V=0;
    gtrash_X=0;

    gnum_trimN=0;
    gnum_trimQ=0;
    gnum_trimV=0;
    gnum_trimA=0;
    gnum_trimT=0;
    gnum_trim5=0;
    gnum_trim3=0;

    gb_totalIn=0;
    gb_totalN=0;
    gb_trimN=0;
    gb_trimQ=0;
    gb_trimV=0;
    gb_trimA=0;
    gb_trimT=0;
    gb_trim5=0;
    gb_trim3=0;

    s=infile;
    GStr infname;
    GStr infname2;
    FILE* f_in=NULL;
    FILE* f_in2=NULL;
    FILE* f_out=NULL;
    FILE* f_out2=NULL;
    bool paired_reads=false;
    setupFiles(f_in, f_in2, f_out, f_out2, s, infname, infname2);
    GLineReader fq(f_in);
    GLineReader* fq2=NULL;
    if (f_in2!=NULL) {
       fq2=new GLineReader(f_in2);
       paired_reads=true;
       }
    RInfo rinfo(f_out, f_out2, &fq, fq2);
    rinfo.infname=infname;
    rinfo.infname2=infname2;
#ifndef NOTHREADS
    GThread *threads=new GThread[num_cpus];
    for (int t=0;t<num_cpus;t++) {
    	threads[t].kickStart(workerThread, &rinfo);
    }
#else
    CTrimHandler* trimmer=new CTrimHandler(&rinfo);
    trimmer->processAll();
    delete trimmer;
#endif

#ifndef NOTHREADS
    for (int i=0;i<num_cpus;i++)
    	threads[i].join();
    delete[] threads;
#endif

    delete fq2;
    FRCLOSE(f_in);
    FRCLOSE(f_in2);
    if (doCollapse) {
       outCounter=0;
       int maxdup_count=1;
       char* maxdup_seq=NULL;
       dhash.startIterate();
       FqDupRec* qd=NULL;
       char* seq=NULL;
       while ((qd=dhash.NextData(seq))!=NULL) {
         GStr rseq(seq);
         //do the dusting here
         if (doDust) {
            int dustbases=dust(rseq);
            if (dustbases>(rseq.length()>>1)) {
               if (trimReport && qd->firstname!=NULL) {
                 fprintf(freport, "%s_x%d\tD\n",qd->firstname, qd->count);
                 }
               gtrash_D+=qd->count;
               continue;
               }
            }
         outCounter++;
         if (qd->count>maxdup_count) {
            maxdup_count=qd->count;
            maxdup_seq=seq;
            }
         if (isfasta) {
           if (prefix.is_empty()) {
             fprintf(f_out, ">%s_x%d\n%s\n", qd->firstname, qd->count,
                           rseq.chars());
             }
           else { //use custom read name
             fprintf(f_out, ">%s%08d_x%d\n%s\n", prefix.chars(), outCounter,
                        qd->count, rseq.chars());
             }
           }
         else { //fastq format
          if (convert_phred) convertPhred(qd->qv, qd->len);
          if (prefix.is_empty()) {
            fprintf(f_out, "@%s_x%d\n%s\n+\n%s\n", qd->firstname, qd->count,
                           rseq.chars(), qd->qv);
            }
          else { //use custom read name
            fprintf(f_out, "@%s%08d_x%d\n%s\n+\n%s\n", prefix.chars(), outCounter,
                        qd->count, rseq.chars(), qd->qv);
            }
           }
         }//for each element of dhash
       if (maxdup_count>1) {
         GMessage("Maximum read multiplicity: x %d (read: %s)\n",maxdup_count, maxdup_seq);
         }
       } //collapse entries
    if (verbose) {
       if (paired_reads) {
           GMessage(">Input files : %s , %s\n", infname.chars(), infname2.chars());
           GMessage("Number of input pairs :%9u\n", inCounter);
           if (onlyTrimmed)
               GMessage("         Output pairs :%9u\t(trimmed only)\n", outCounter);
           else
        	   GMessage("         Output pairs :%9u\t(%u discarded)\n", outCounter, inCounter-outCounter);
           }
         else {
           GMessage(">Input file : %s\n", infname.chars());
           GMessage("Number of input reads :%9d\n", inCounter);
           GMessage("         Output reads :%9d  (%u discarded)\n", outCounter, inCounter-outCounter);
           }
       GMessage("\n-------------- Read trimming: --------------\n");
       if (gnum_trim5)
          GMessage("           5' trimmed :%9u\n", gnum_trim5);
       if (gnum_trim3)
          GMessage("           3' trimmed :%9u\n", gnum_trim3);
       if (gnum_trimQ)
          GMessage("         q.v. trimmed :%9u\n", gnum_trimQ);
       if (gnum_trimN)
          GMessage("            N trimmed :%9u\n", gnum_trimN);
       if (gnum_trimT)
          GMessage("       poly-T trimmed :%9u\n", gnum_trimT);
       if (gnum_trimA)
          GMessage("       poly-A trimmed :%9u\n", gnum_trimA);
       if (gnum_trimV)
          GMessage("      Adapter trimmed :%9u\n", gnum_trimV);
       GMessage("--------------------------------------------\n");
       if (gtrash_s>0)
         GMessage("Trashed by initial len:%9d\n", gtrash_s);
       if (gtrash_N>0)
         GMessage("         Trashed by N%%:%9d\n", gtrash_N);
       if (gtrash_Q>0)
         GMessage("Trashed by low quality:%9d\n", gtrash_Q);
       if (gtrash_poly>0)
         GMessage("   Trashed by poly-A/T:%9d\n", gtrash_poly);
       if (gtrash_V>0)
         GMessage("    Trashed by adapter:%9d\n", gtrash_V);
       if (gtrash_X>0)
         GMessage("    Trashed by X      :%9d\n", gtrash_X);
     GMessage("\n-------------- Base counts: ----------------\n");
       GMessage("      Input bases :%12llu\n", gb_totalIn);
       double percN=100.0* ((double)gb_totalN/(double)gb_totalIn);
       GMessage("          N bases :%12llu (%4.2f%%)\n", gb_totalN, percN);
       GMessage("   trimmed from 5':%12llu\n", gb_trim5);
       GMessage("   trimmed from 3':%12llu\n", gb_trim3);
       GMessage("\n");
       if (gb_trimQ)
       GMessage("     q.v. trimmed :%12llu\n", gb_trimQ);
       if (gb_trimN)
       GMessage("        N trimmed :%12llu\n", gb_trimN);
       if (gb_trimT)
       GMessage("   poly-T trimmed :%12llu\n", gb_trimT);
       if (gb_trimA)
       GMessage("   poly-A trimmed :%12llu\n", gb_trimA);
       if (gb_trimV)
       GMessage("  Adapter trimmed :%12llu\n", gb_trimV);

       }
    FWCLOSE(f_out);
    FWCLOSE(f_out2);
   } //while each input file
  if (trimReport) {
          FWCLOSE(freport);
          }
  //getc(stdin);
}

class NData {
 public:
   GVec<int> NPos; //there should be no reads longer than 1K ?
   //int NCount;
   int end5;
   int end3;
   int n5; //left side N position (index in NPos)
   int n3; //right side N position (index in NPos)
   int seqlen;
   double perc_N; //percentage of Ns in end5..end3 range only!
   const char* seq;
   bool valid;
   NData():NPos(),end5(0),end3(0),n5(0),n3(-1),seqlen(0),
         perc_N(0),seq(NULL),valid(true) {  }
   NData(GStr& rseq):NPos(rseq.length()), end5(0),end3(rseq.length()-1),n5(0),n3(-1),
       seqlen(rseq.length()), perc_N(0),seq(rseq.chars()),valid(true) {
     //init(rseq);
     for (int i=0;i<seqlen;i++)
        if (seq[i]=='N') {// if (!ichrInStr(rseq[i], "ACGT")
           NPos.Add(i);
           }
     n3=NPos.Count()-1; // -1 if no Ns
     N_calc();
   }
  void N_trim(); //former N_analyze();
  double N_calc() { //only in the end5-end3 region
     if (n5<=n3) {
       perc_N=((n3-n5+1)*100.0)/(end3-end5+1);
       }
      else perc_N=0; 
    return perc_N;
  }
 };


void NData::N_trim() { //N_analyze(NData& feat, int l5, int l3, int p5, int p3) {
/* assumes feat was filled properly */
 int old_dif, t5,t3,v;
 int l3=end3;
 int l5=end5;
 while (l3>=l5+2 && n5<=n3) {
   t5=NPos[n5]-l5; //left side possible trimming
   t3=l3-NPos[n3]; //right side potential trimming
   old_dif=n3-n5;
   if (dist_lenN) { 
      v=dist_lenN;
   }
   else {
     v=iround(perc_lenN*(l3-l5+1)/100);
     if (v>20) v=20; // enforce N-search limit for very long reads
        else if (v<1) v=1;
   }   
   if (t5 <= v ) {
     l5=NPos[n5]+1;
     n5++; //we can trim at 5' end up to after leftmost N
   }
   if (t3 <= v) {
     l3=NPos[n3]-1;
     n3--; //we can trim at 3' before leftmost N;
   }
   // restNs=p3-p5; number of Ns in the new CLR 
   if (n3-n5==old_dif) { // no change, return
     break;
   }
 }
 end5=l5;
 end3=l3;
 N_calc();
 return;
 /*
 if (l3<l5+2 || p5>p3 ) {
   feat.end5=l5+1;
   feat.end3=l3+1;
   return;
   }

 t5=feat.NPos[p5]-l5; //left side possible trimming
 t3=l3-feat.NPos[p3]; //right side potential trimming
 old_dif=p3-p5;
 v=(int)((((double)(l3-l5))*perc_lenN)/100);
 if (v>20) v=20; // enforce N-search limit for very long reads
    else if (v<1) v=1;
 if (t5 < v ) {
   l5=feat.NPos[p5]+1;
   p5++; //we can trim at 5' end up to after leftmost N
   }
 if (t3 < v) {
   l3=feat.NPos[p3]-1;
   p3--; //we can trim at 3' before leftmost N;
   }
 // restNs=p3-p5; number of Ns in the new CLR 
 if (p3-p5==old_dif) { // no change, return
           feat.end5=l5+1;
           feat.end3=l3+1;
           return;
           }
    else
      N_analyze(feat, l5,l3, p5,p3);
 */
}


bool CTrimHandler::qtrim(GStr& qvs, int &l5, int &l3) {
if (qvtrim_qmin==0 || qvs.is_empty()) return false;
l5=0;
l3=qvs.length()-1;
if (qv_phredtype==0) {
  //try to guess the Phred type
  int vmin=256, vmax=0;
  for (int i=0;i<qvs.length();i++) {
     if (vmin>qvs[i]) vmin=qvs[i];
     if (vmax<qvs[i]) vmax=qvs[i];
     }
  if (vmin<64) { qv_phredtype=33; qv_cvtadd=31; }
  if (vmax>95) { qv_phredtype=64; qv_cvtadd=-31; }
  if (qv_phredtype==0) {
    GError("Error: couldn't determine Phred type, please use the -p33 or -p64 !\n");
    }
  if (verbose)
    GMessage("Input reads have Phred-%d quality values.\n", (qv_phredtype==33 ? 33 : 64));
} //guessing Phred type
int winlen=GMIN(qvtrim_win, qvs.length()/4);
if (winlen<3) {
 //no sliding window
 //scan from the ends and look for two consecutive bases above the threshold
 for (;l3>2;l3--) {
    if (qvs[l3]-qv_phredtype>=qvtrim_qmin && qvs[l3-1]-qv_phredtype>=qvtrim_qmin) break;
 }
// qtrim 5' end
 for (l5=0;l5<qvs.length()-3;l5++) {
    if (qvs[l5]-qv_phredtype>=qvtrim_qmin && qvs[l5+1]-qv_phredtype>=qvtrim_qmin) break;
 }
}
else {
 // trim 3'
 //sliding window from the 5' end until avg qual drops below the threshold
 //init sum

 int qsum=0;
 /*
 int qilow=-1; //first base index where qv drops below qmin
 for (int q=0;q<winlen;q++) {
   int qvq=qvs[q]-qv_phredtype;
   qsum+=qvq;
   if (qilow<0 && qvq<qvtrim_qmin) qilow=q;
   }
 double qavg=((double)qsum)/winlen;
 if (qavg<qvtrim_qmin) { //first window fail
   l3=qilow-1;
 }
 else {
   for (int i=1;i<=qvs.length()-qvtrim_win;i++) {
     qsum -= qvs[i-1]-qv_phredtype;
     int inew=i+qvtrim_win-1;
     int qvnew=qvs[inew]-qv_phredtype;
     qsum += qvnew;
     //if (qilow<i && qvnew<qvtrim_qmin) qilow=inew;
     qavg=((double)qsum)/qvtrim_win;
     //GMessage("i=%d (%c), inew=%d (%c), qilow=%d, qavg=%4.2f\n", i, qvs[i], inew, qvs[inew], qilow, qavg);
     if (qavg<qvtrim_qmin) {
       for (int qlo=i;qlo<i+qvtrim_win;qlo++) {
          if (qvs[qlo]-qv_phredtype<qvtrim_qmin) {
            l3=qlo-1;
            break;
          }
       }
       //l3=qilow-1;
       break;
       }
   } //for each sliding window
 }
 */
 int qi5=l5; //suggested qv trim 5' base index
 int qi3=l3; //suggested qv trim 3' base index
 double qavg; //avg. qv for the current window
 int i5bw=-1; //index of first base below threshold in current window
 int i3bw=-1; //index of last base below threshold in current window
 bool okfound=false; //found a window above threshold
 for (int i=0;i<winlen;++i) {
   int cq=qvs[i]-qv_phredtype;
   qsum+=cq;
   if (cq<qvtrim_qmin) {
	   i3bw=i;
	   if (i5bw<0) i5bw=i;
   }
 }
 qavg=((double)qsum)/winlen;
 if (iround(qavg)<qvtrim_qmin) {
	 //propose 5' trimming by qv
	 qi5=i3bw+1;
 }
 else { okfound=true; }
 //now scan the rest of the read
 if (okfound) i5bw=-1;
 for (int i=1;i<=qvs.length()-winlen;i++) {
   if (i5bw<i) i5bw=-1;
   qsum -= qvs[i-1]-qv_phredtype;
   int inew=i+winlen-1;
   int qvnew=qvs[inew]-qv_phredtype;
   if (qvnew<qvtrim_qmin) {
      if (i5bw<0) i5bw=inew;
      i3bw=inew;
   }
   qsum += qvnew;
   //if (qilow<i && qvnew<qvtrim_qmin) qilow=inew;
   qavg=((double)qsum)/winlen;
   if (qavg<qvtrim_qmin) { //bad qv window
	 if (okfound) {
		 //trimming 3' now
		 qi3=i5bw-1;
	     break;
	 } else {
		 //still trimming 5', shame
		 qi5=i3bw+1;
		 if (qvs.length()-qi5<min_read_len)
			 break;
	 }
   }
   else okfound=true;
 } //for each sliding window
 if (!okfound) {
	 //fatal trimming at 5' end
	 l5=qi5;
	 return true;
 }
 l5=qi5;
 l3=qi3;
}

if (qvtrim_max>0) {
  if (qvs.length()-1-l3>qvtrim_max) l3=qvs.length()-1-qvtrim_max;
  if (l5>qvtrim_max) l5=qvtrim_max;
  }
return (l5>0 || l3<qvs.length()-1);
}

bool CTrimHandler::ntrim(GStr& rseq, int &l5, int &l3, double& pN) {
 //count Ns in the sequence, trim N-rich ends
 NData feat(rseq);
 l5=feat.end5;
 l3=feat.end3;
 pN=0.0;
 if (feat.NPos.Count()==0) return false;
 //int clrNcount = N_analyze(feat, feat.end5-1, feat.end3-1, 0, feat.NPos.Count()-1); //feat.NCount-1);
 feat.N_trim(); //tries to trim terminal Ns, recalculates perc_N
 pN=feat.perc_N;
 if (l5==feat.end5 && l3==feat.end3) {
    if (feat.perc_N>max_perc_N) {
           #ifdef TRIMDEBUG
           GMessage(" ### : N_trim() did nothing but remaining range %d-%d has internal %N = %4.2f\n", 
               feat.end5, feat.end3, feat.perc_N);
           #endif
           feat.valid=false;
           return true;
           }
      else {
       return false; //no trimming
       }
    }
 l5=feat.end5;
 l3=feat.end3;
 //feat.N_calc(); feat.N_trim() did this already
 #ifdef TRIMDEBUG
     GStr r=rseq.substr(feat.end5, feat.end3-feat.end5+1);
     GMessage(" ### : after N_trim() clear range %d-%d has %N = %4.2f :\n%s\n", 
          feat.end5, feat.end3, feat.perc_N, r.chars());
 #endif
 /*
  if (l3-l5+1<min_read_len) {
   feat.valid=false;
   return true;
   }
 if (feat.perc_N>max_perc_N) {
      feat.valid=false;
      return true;
      }
 */
 return true;
 }

//--------------- dust functions ----------------
class DNADuster {
 public:
  int dustword;
  int dustwindow;
  int dustwindow2;
  int dustcutoff;
  int mv, iv, jv;
  int counts[32*32*32];
  int iis[32*32*32];
  DNADuster(int cutoff=16, int winsize=32, int wordsize=3) {
    dustword=wordsize;
    dustwindow=winsize;
    dustwindow2 = (winsize>>1);
    dustcutoff=cutoff;
    mv=0;
    iv=0;
    jv=0;
    }
  void setWindowSize(int value) {
    dustwindow = value;
    dustwindow2 = (dustwindow >> 1);
    }
  void setWordSize(int value) {
    dustword=value;
    }
void wo1(int len, const char* s, int ivv) {
  int i, ii, j, v, t, n, n1, sum;
  int js, nis;
  n = 32 * 32 * 32;
  n1 = n - 1;
  nis = 0;
  i = 0;
  ii = 0;
  sum = 0;
  v = 0;
  for (j=0; j < len; j++, s++) {
        ii <<= 5;
        if (*s<=32) {
           i=0;
           continue;
           }
        ii |= *s - 'A'; //assume uppercase!
        ii &= n1;
        i++;
        if (i >= dustword) {
              for (js=0; js < nis && iis[js] != ii; js++) ;
              if (js == nis) {
                    iis[nis] = ii;
                    counts[ii] = 0;
                    nis++;
              }
              if ((t = counts[ii]) > 0) {
                    sum += t;
                    v = 10 * sum / j;
                    if (mv < v) {
                          mv = v;
                          iv = ivv;
                          jv = j;
                    }
              }
              counts[ii]++;
        }
  }
}

int wo(int len, const char* s, int* beg, int* end) {
      int i, l1;
      l1 = len - dustword + 1;
      if (l1 < 0) {
            *beg = 0;
            *end = len - 1;
            return 0;
            }
      mv = 0;
      iv = 0;
      jv = 0;
      for (i=0; i < l1; i++) {
            wo1(len-i, s+i, i);
            }
      *beg = iv;
      *end = iv + jv;
      return mv;
 }

void dust(const char* seq, char* seqmsk, int seqlen, int cutoff=0) { //, maskFunc maskfn) {
  int i, j, l, a, b, v;
  if (cutoff==0) cutoff=dustcutoff;
  a=0;b=0;
  //GMessage("Dust cutoff=%d\n", cutoff);
  for (i=0; i < seqlen; i += dustwindow2) {
        l = (seqlen > i+dustwindow) ? dustwindow : seqlen-i;
        v = wo(l, seq+i, &a, &b);
        if (v > cutoff) {
           //for (j = a; j <= b && j < dustwindow2; j++) {
           for (j = a; j <= b; j++) {
                    seqmsk[i+j]='N';//could be made lowercase instead
                    }
           }
         }
//return first;
 }
};

//static DNADuster duster;

int dust(GStr& rseq) {
 DNADuster duster;
 char* seq=Gstrdup(rseq.chars());
 duster.dust(rseq.chars(), seq, rseq.length(), dust_cutoff);
 //check the number of Ns:
 int ncount=0;
 for (int i=0;i<rseq.length();i++) {
   if (seq[i]=='N') ncount++;
   }
 if (dustMask) rseq=seq; //hard masking requested
 GFREE(seq);
 return ncount;
 }

struct SLocScore {
  int pos;
  int score;
  SLocScore(int p=0,int s=0) {
    pos=p;
    score=s;
    }
  void set(int p, int s) {
    pos=p;
    score=s;
    }
  void add(int p, int add) {
    pos=p;
    score+=add;
    }
};

bool CTrimHandler::trim_poly3(GStr &seq, int &l5, int &l3, const char* poly_seed) {
 if (!doPolyTrim) return false;
 int rlen=seq.length();
 l5=0;
 l3=rlen-1;
 int32 seedVal=*(int32*)poly_seed;
 char polyChar=poly_seed[0];
 //assumes N trimming was already done
 //so a poly match should be very close to the end of the read
 // -- find the initial match (seed)
 int lmin=GMAX((rlen-16), 0);
 int li;
 for (li=rlen-4;li>lmin;li--) {
   if (seedVal==*(int*)&(seq[li])) {
      break;
      }
   }
 if (li<=lmin) return false;
 //seed found, try to extend it both ways
 //extend right
 int ri=li+3;
 SLocScore loc(ri, poly_m_score<<2);
 SLocScore maxloc(loc);
 //extend right
 while (ri<rlen-1) {
   ri++;
   if (seq[ri]==polyChar) {
                loc.add(ri,poly_m_score);
                }
   else if (seq[ri]=='N') {
                loc.add(ri,0);
                }
   else { //mismatch
        loc.add(ri,poly_mis_score);
        if (maxloc.score-loc.score>poly_dropoff_score) break;
        }
   if (maxloc.score<=loc.score) {
      maxloc=loc;
      }
   }
 ri=maxloc.pos;
 if (ri<rlen-6) return false; //no trimming wanted, too far from 3' end
 //ri = right boundary for the poly match
 //extend left
 loc.set(li, maxloc.score);
 maxloc.pos=li;
 while (li>0) {
    li--;
    if (seq[li]==polyChar) {
                 loc.add(li,poly_m_score);
                 }
    else if (seq[li]=='N') {
                 loc.add(li,0);
                 }
    else { //mismatch
         loc.add(li,poly_mis_score);
         if (maxloc.score-loc.score>poly_dropoff_score) break;
         }
    if (maxloc.score<=loc.score) {
       maxloc=loc;
       }
    }
li=maxloc.pos;
if ((maxloc.score==poly_minScore && ri==rlen-1) ||
    (maxloc.score>poly_minScore && ri>=rlen-3) ||
    (maxloc.score>(poly_minScore*3) && ri>=rlen-8)) {
  //trimming this li-ri match at 3' end
    l3=li-1;
    if (l3<0) l3=0;
    return true;
    }
return false;
}

bool CTrimHandler::trim_poly5(GStr &seq, int &l5, int &l3, const char* poly_seed) {
 if (!doPolyTrim) return false;
 int rlen=seq.length();
 l5=0;
 l3=rlen-1;
 int32 seedVal=*(int32*)poly_seed;
 char polyChar=poly_seed[0];
 //assumes N trimming was already done
 //so a poly match should be very close to the end of the read
 // -- find the initial match (seed)
 int lmax=GMIN(12, rlen-4);//how far from 5' end to look for 4-mer seeds
 int li;
 for (li=0;li<=lmax;li++) {
   if (seedVal==*(int*)&(seq[li])) {
      break;
      }
   }
 if (li>lmax) return false;
 //seed found, try to extend it both ways
 //extend left
 int ri=li+3; //save rightmost base of the seed
 SLocScore loc(li, poly_m_score<<2);
 SLocScore maxloc(loc);
 while (li>0) {
    li--;
    if (seq[li]==polyChar) {
                 loc.add(li,poly_m_score);
                 }
    else if (seq[li]=='N') {
                 loc.add(li,0);
                 }
    else { //mismatch
         loc.add(li,poly_mis_score);
         if (maxloc.score-loc.score>poly_dropoff_score) break;
         }
    if (maxloc.score<=loc.score) {
       maxloc=loc;
       }
    }
 li=maxloc.pos;
 if (li>5) return false; //no trimming wanted, too far from 5' end
 //li = right boundary for the poly match

 //extend right
 loc.set(ri, maxloc.score);
 maxloc.pos=ri;
 while (ri<rlen-1) {
   ri++;
   if (seq[ri]==polyChar) {
                loc.add(ri,poly_m_score);
                }
   else if (seq[ri]=='N') {
                loc.add(ri,0);
                }
   else { //mismatch
        loc.add(ri,poly_mis_score);
        if (maxloc.score-loc.score>poly_dropoff_score) break;
        }
   if (maxloc.score<=loc.score) {
      maxloc=loc;
      }
   }
ri=maxloc.pos;
if ((maxloc.score==poly_minScore && li==0) ||
     (maxloc.score>poly_minScore && li<2)
     || (maxloc.score>(poly_minScore*3) && li<8)) {
    //adjust l5 to reflect this trimming of 5' end
    l5=ri+1;
    if (l5>rlen-1) l5=rlen-1;
    return true;
    }
return false;
}

bool CTrimHandler::trim_adapter3(GStr& seq, int&l5, int &l3, int& aidx) {
 if (adapters3.Count()==0) return false;
 //GMessage("Trimming adapter 3!\n");
 int rlen=seq.length();
 l5=0;
 l3=rlen-1;
 bool trimmed=false;
 GStr wseq(seq);
 int wlen=rlen;
 GXSeqData seqdata;
 int numruns=revCompl ? 2 : 1;
 GList<GXAlnInfo> bestalns(true, true, false);
 aidx=-1;
 for (int ai=0;ai<adapters3.Count();ai++) {
   for (int r=0;r<numruns;r++) {
     if (r) {
  	  seqdata.update(adapters3[ai]->seqr.chars(), adapters3[ai]->seqr.length(),
  		 adapters3[ai]->pzr, wseq.chars(), wlen, adapters3[ai]->amlen);
        }
     else {
  	    seqdata.update(adapters3[ai]->seq.chars(), adapters3[ai]->seq.length(),
  		 adapters3[ai]->pz, wseq.chars(), wlen, adapters3[ai]->amlen);
        }
     //GXAlnInfo* aln=match_adapter(seqdata, adapters3[ai]->trim_type, minEndAdapter, gxmem_r, min_pid3);
     GXAlnInfo* aln=match_adapter(seqdata, galn_TrimRight, minEndAdapter, gxmem_r, min_pid3);
	 if (aln) {
	   aln->udata=adapters3[ai]->fidx;
	   if (aln->strong) {
		   trimmed=true;
		   bestalns.Add(aln);
		   break; //will check the rest next time
		   }
	    else bestalns.Add(aln);
	   }
   }//forward and reverse adapters
   if (trimmed) break; //will check the rest in the next cycle
  }//for each 3' adapter
 if (bestalns.Count()>0) {
	   GXAlnInfo* aln=bestalns[0];
	   if (aln->sl-1 > wlen-aln->sr) {
		   //keep left side
		   l3-=(wlen-aln->sl+1);
		   if (l3<0) l3=0;
		   }
	   else { //keep right side
		   l5+=aln->sr;
		   if (l5>=rlen) l5=rlen-1;
		   }
	   //delete aln;
	   //if (l3-l5+1<min_read_len) return true;
	   wseq=seq.substr(l5,l3-l5+1);
	   wlen=wseq.length();
	   aidx=aln->udata;
	   return true; //break the loops here to report a good find
     }
  aidx=-1;
  return false;
 }

bool CTrimHandler::trim_adapter5(GStr& seq, int&l5, int &l3, int& aidx) {
 if (adapters5.Count()==0) return false;
 int rlen=seq.length();
 l5=0;
 l3=rlen-1;
 bool trimmed=false;
 GStr wseq(seq);
 int wlen=rlen;
 GXSeqData seqdata;
 int numruns=revCompl ? 2 : 1;
 GList<GXAlnInfo> bestalns(true, true, false);
 aidx=-1;
 for (int ai=0;ai<adapters5.Count();ai++) {
   for (int r=0;r<numruns;r++) {
     if (r) {
  	  seqdata.update(adapters5[ai]->seqr.chars(), adapters5[ai]->seqr.length(),
  		 adapters5[ai]->pzr, wseq.chars(), wlen, adapters5[ai]->amlen);
        }
     else {
  	    seqdata.update(adapters5[ai]->seq.chars(), adapters5[ai]->seq.length(),
  		 adapters5[ai]->pz, wseq.chars(), wlen, adapters5[ai]->amlen);
        }
	 //GXAlnInfo* aln=match_adapter(seqdata, adapters5[ai]->trim_type,
     GXAlnInfo* aln=match_adapter(seqdata, galn_TrimLeft,
		                                       minEndAdapter, gxmem_l, min_pid5);
	 if (aln) {
	   aln->udata=adapters5[ai]->fidx;
	   if (aln->strong) {
		   trimmed=true;
		   bestalns.Add(aln);
		   break; //will check the rest next time
		   }
	    else bestalns.Add(aln);
	   }
	 } //forward and reverse?
   if (trimmed) break; //will check the rest in the next cycle
  }//for each 5' adapter
  if (bestalns.Count()>0) {
	   GXAlnInfo* aln=bestalns[0];
	   if (aln->sl-1 > wlen-aln->sr) {
		   //keep left side
		   l3-=(wlen-aln->sl+1);
		   if (l3<0) l3=0;
		   }
	   else { //keep right side
		   l5+=aln->sr;
		   if (l5>=rlen) l5=rlen-1;
		   }
	   //delete aln;
	   //if (l3-l5+1<min_read_len) return true;
	   wseq=seq.substr(l5,l3-l5+1);
	   wlen=wseq.length();
	   aidx=aln->udata;
	   return true; //break the loops here to report a good find
     }
  aidx=-1;
  return false;
}

//convert qvs to/from phred64 from/to phread33
void convertPhred(GStr& q) {
 for (int i=0;i<q.length();i++) q[i]+=qv_cvtadd;
}

void convertPhred(char* q, int len) {
 for (int i=0;i<len;i++) q[i]+=qv_cvtadd;
}

bool getFastxRead(GLineReader& fq, RData& rd, GStr& infname) {
	 if (fq.eof()) return false;
	 char* l=fq.getLine();
	 while (l!=NULL && (l[0]==0 || isspace(l[0]))) l=fq.getLine(); //ignore empty lines
	 if (l==NULL) return false;
	 /* if (rawFormat) {
	      //TODO: implement raw qseq parsing here?
	      //if (raw type=N) then continue; //skip invalid/bad records
	      } //raw qseq format
	 else { // FASTQ or FASTA */
	 isfasta=(l[0]=='>');
	 if (!isfasta && l[0]!='@') GError("Error: fasta/fastq record marker not found(%s)\n%s\n",
	      infname.chars(), l);
	 GStr s(l);
	 rd.rid=&(l[1]);
	 for (int i=0;i<rd.rid.length();i++)
	    if (rd.rid[i]<=' ') {
	       if (i<rd.rid.length()-2) rd.rinfo=rd.rid.substr(i+1);
	       rd.rid.cut(i);
	       break;
	       }
	  //now get the sequence
	 if ((l=fq.getLine())==NULL)
	      GError("Error: unexpected EOF after header for read %s (%s)\n",
	      		rd.rid.chars(), infname.chars());
	 rd.seq=l; //this must be the DNA line
	 while ((l=fq.getLine())!=NULL) {
	      //seq can span multiple lines
	      if (l[0]=='>' || l[0]=='+') {
	           fq.pushBack();
	           break; //
	           }
	      rd.seq+=l;
	      } //check for multi-line seq
	 if (!isfasta) { //reading fastq quality values, which can also be multi-line
	    if ((l=fq.getLine())==NULL)
	        GError("Error: unexpected EOF after sequence for %s\n", rd.rid.chars());
	    if (l[0]!='+') GError("Error: fastq qv header marker not detected!\n");
	    if ((l=fq.getLine())==NULL)
	        GError("Error: unexpected EOF after qv header for %s\n", rd.rid.chars());
	    rd.qv=l;
	    //if (rqv.length()!=rseq.length())
	    //  GError("Error: qv len != seq len for %s\n", rname.chars());
	    while (rd.qv.length()<rd.seq.length() && ((l=fq.getLine())!=NULL)) {
	      rd.qv+=l; //append to qv string
	      }
	    }// fastq
	 if (rd.seq.is_empty()) {
		 rd.seq="A";
		 rd.qv="B";
	 }
	 // } //<-- FASTA or FASTQ
	 rd.seq.upper();
	 return true;
}

bool getBufRead(GVec<RData>& rbuf, int& rbuf_p, GLineReader* fq, GStr& infname, RData& rdata) {
	rdata.clear();
	if (rbuf_p<=0) { //load next chunk of reads
		rbuf_p=0;
		if (fq->isEof()) return false;
		{
#ifndef NOTHREADS
			GLockGuard<GFastMutex> guard(readMutex);
#endif
			while(!fq->isEof() && rbuf_p<readBufSize) {
				if (!getFastxRead(*(fq), rdata, infname)) break;
				rbuf.Add(&rdata);
				//rbuf[rbuf_p]=rdata;
				++rbuf_p;
			}
		}
		if (rbuf_p==0) return false;
		//if (rbuf_p<rbuf.Count()) rbuf.setCount(rbuf_p);
		GASSERT( rbuf_p==rbuf.Count() );
	} //read next chunk of reads
	//next unprocessed read from read buffer
	rdata=rbuf[rbuf.Count()-rbuf_p];
	rbuf_p--;
	return true;
}

bool CTrimHandler::fetchReads() {
	// do NOT call this unless rbuf_p<=0, it'll clobber the buffer!
	GASSERT(rbuf_p<=0);
	//refill read buffer
	rbuf_p=0;
	rbuf2_p=0;
	if (rinfo->fq->isEof()) return false;
	{ //file reading block
#ifndef NOTHREADS
		GLockGuard<GFastMutex> guard(readMutex);
#endif
		while(!rinfo->fq->isEof() && rbuf_p<readBufSize) {
			RData rd;
			if (!getFastxRead(*(rinfo->fq), rd, rinfo->infname)) break;
			rbuf.Add(rd);
			//rbuf[rbuf_p]=rd;
			++rbuf_p;
		}
		if (rbuf_p==0) return false;
		GASSERT( rbuf_p==rbuf.Count() );
		//also load from the mates file, if given
		if (rinfo->fq2) {
			while(!rinfo->fq2->isEof() && rbuf2_p<readBufSize) {
				RData rd;
				if (!getFastxRead(*(rinfo->fq2), rd, rinfo->infname2)) break;
				rbuf2.Add(rd);
				//rbuf[rbuf_p]=rd;
				++rbuf2_p;
			}
			if (rbuf_p!=rbuf2_p) {
				GError("Error: mismatch in the count of reads vs mates!\n");
			}
			GASSERT( rbuf2_p==rbuf2.Count() );
		} //loaded the mates too
	} //file reading block
	return true;
}

bool CTrimHandler::nextRead(RData* & rdata, RData* & rdata2) {
  /*
  if (!getBufRead(rbuf, rbuf_p, rinfo->fq, rinfo->infname, rdata)) return false;
  incounter++;
  if (rinfo->fq2==NULL) return true;
  getBufRead(rbuf2, rbuf2_p, rinfo->fq2, rinfo->infname2, rdata2);
  */
  //rdata=NULL;
  //rdata2=NULL;
  GASSERT(rbuf_p>0); //read buffer should be prepared for use!
  GASSERT(rbuf.Count()>0);
  //if (rbuf_p<=0) {
  //  if (!fetchReads()) return false;
  //}
  if (rbuf.Count()==0 || rbuf_p<=0) return false;
  ++incounter;
  rdata=&(rbuf[rbuf.Count()-rbuf_p]);
  --rbuf_p;
  if (rinfo->fq2 && rbuf2.Count()>0) {
	rdata2=&(rbuf2[rbuf2.Count()-rbuf2_p]);
	--rbuf2_p;
  }
  return true;
}

void CTrimHandler::flushReads() {
	 //write reads and update counts
	 for (int i=0;i<rbuf.Count();++i) {
		RData& rd=rbuf[i];
		bool trimmed=(rd.trashcode>0);
		if (rd.trashcode>0 && trimReport)
			trim_report(rd);
		RData rd2;
		RData *rd2p=&rd2;
		if (rinfo->fq2 && rbuf2.Count()>i) {
			rd2p = & (rbuf2[i]);
		}
		if (!rd2p->seq.is_empty() && rd2p->trashcode>0 && trimReport) {
			trim_report(*rd2p, 1);
			trimmed=true;
		}
		//decide what to do with this pair and print rd2.seq if any read in the pair makes it
		/*
		if (pairedOutput) {
			if (rd.trashcode>1 && rd2p->trashcode<=1) {
				rd.trashcode=1; //rescue read
			}
			else if (rd.trashcode<=1 && rd2p->trashcode>1) {
				rd2p->trashcode=1; //rescue mate
			}
		}
		*/
		if (!doCollapse) {
		  if ((onlyTrimmed && trimmed) || !onlyTrimmed )
		      writeRead(rd, *rd2p);
		}
	 }
	 updateCounts();
	 Clear();
}

void showTrim(RData& r) {
  if (r.trim5>0 || r.l3()==0) {
    color_bg(c_red);
    }
  for (int i=0;i<r.seq.length()-1;i++) {
    if (i && i==r.trim5) color_resetbg();
    fprintf(stderr, "%c", r.seq[i]);
    if (i && i==r.l3()) color_bg(c_red);
   }
  fprintf(stderr, "%c", r.seq[r.seq.length()-1]);
  color_reset();
  fprintf(stderr, "\n");
}

void showTrim(GStr& rname, GStr& s, int l5, int l3,  GVec<STrimOp>& t_hist) {
 fprintf(stdout, ">%s", rname.chars());
 if (t_hist.Count()>0) fprintf(stdout,"\t");
 for (int i=0;i<t_hist.Count();i++) {
  fprintf(stdout,"%d%c%d",t_hist[i].tend, t_hist[i].tcode, t_hist[i].tlen);
  if (i<t_hist.Count()-1) fprintf(stdout,",");
 }
 fprintf(stdout, "\n");
   if (l5>0 || l3==0) {
    color_bg(c_red, stdout);
    }
  for (int i=0;i<s.length()-1;i++) {
    if (i && i==l5) color_resetbg(stdout);
    fprintf(stdout, "%c", s[i]);
    if (i && i==l3) color_bg(c_red, stdout);
   }
  fprintf(stdout, "%c", s[s.length()-1]);
  color_reset(stdout);
  fprintf(stdout, "\n");
}



struct STrimState {
 int w5;
 int w3;
 GStr wseq;
 GStr wqv;
 bool w3upd;
 bool w5upd;
 bool wupd;
 STrimState(GStr& rseq, GStr& rqv):w5(0), w3(rseq.length()-1),
     wseq(rseq.chars()), wqv(rqv.chars()), w3upd(true), w5upd(true), wupd(true) {
 }

 char update(char trim_code, int& trim5, int& trim3) {
   trim5+=w5;
   trim3+=(wseq.length()-1-w3);
 //#ifdef TRIMDEBUG
 //  GMessage("#### TRIM by '%c' code ( w5-w3 = %d-%d ):\n",trim_code, w5,w3);
 //  showTrim(wseq, wqv, w5, w3);
 //#endif
   //-- keep only the w5..w3 range
   wseq=wseq.substr(w5, w3-w5+1);
   if (!wqv.is_empty())
      wqv=wqv.substr(w5, w3-w5+1);
   if (w3-w5+1<min_read_len) {
       return trim_code; //return last operation code as "trash code"
   }
   w5=0;
   w3=wseq.length()-1;
   return 0;
 }
 
};

char CTrimHandler::process_read (RData &r) {
 //returns 0 if the read was untouched, 1 if it was just trimmed
 // and a trash code if it was trashed
 if (r.seq.length()-r.trim5-r.trim3<min_read_len) {
   return 's'; //too short already
   }
//count Ns
b_totalIn+=r.seq.length();
for (int i=0;i<r.seq.length();i++) {
 if (isACGT[(int)r.seq[i]]==0) b_totalN++;
 }
double percN=0;
char trim_code=0;

GStr wseq(r.seq.chars());
GStr wqv(r.qv.chars());

int w5=r.trim5;
int w3=r.seq.length()-r.trim3-1;

//first do the q-based trimming
if (qvtrim_qmin!=0 && !wqv.is_empty() && qtrim(wqv, w5, w3)) { // qv-threshold trimming
   trim_code='Q';
   int t5=(w5-r.trim5);
   if (t5>0) {
      STrimOp trimop(5,trim_code,t5);
      r.trimhist.Add(trimop);
   }
   int t3=(r.l3()-w3);
   if (t3>0) {
      STrimOp trimop(3,trim_code,t3);
      r.trimhist.Add(trimop);
   }
   #ifdef TRIMDEBUG
     GMessage("#DBG# qv trimming: %d from 5'end; %d from 3'end\n",t5,t3);
   #endif
   b_trimQ+=t5+t3;
   num_trimQ++;
   r.trim5=w5;
   r.trim3=r.seq.length()-1-w3;
   if (r.seq.length()-r.trim5-r.trim3<min_read_len) {
     return trim_code; //invalid read
     }
   //-- keep only the w5..w3 range
   wseq=wseq.substr(r.trim5, r.seq.length()-r.trim3-r.trim5);
   if (!wqv.is_empty())
      wqv=wqv.substr(r.trim5, r.seq.length()-r.trim3-r.trim5);
   } //qv trimming
// N-trimming on the remaining read seq
if (ntrim(wseq, w5, w3, percN)) {
   //Note: ntrim sets w5 to the number of trimmed bases at read start
   //     and w3 to the new end of read sequence
#ifdef TRIMDEBUG
   GMessage("#DBG# N trim: keeping %d-%d range: %s\n",w5+1,w3, wseq.substr(w5, w3-w5+1).chars() );
#endif
   int trim3=(wseq.length()-1-w3);
   trim_code='N';
   b_trimN+=w5+trim3;
   num_trimN++;
   if (w5>0) {
      STrimOp trimop(5,trim_code,w5);
      r.trimhist.Add(trimop);
   }
   if (trim3>0) {
      STrimOp trimop(3,trim_code,trim3);
      r.trimhist.Add(trimop);
   }
   r.trim5+=w5;
   r.trim3+=trim3;
   if (w3-w5+1<min_read_len) {
     return trim_code; //to be trashed
   }
   if (percN > max_perc_N) {
     return trim_code;
   }
    //-- keep only the w5..w3 range
   wseq=wseq.substr(w5, w3-w5+1);
   if (!wqv.is_empty())
      wqv=wqv.substr(w5, w3-w5+1);
   //w5=0;
   //w3=wseq.length()-1;
}

//clean the more dirty end first - 3'
bool trimmedA=false;
bool trimmedT=false;
bool trimmedV=false;
STrimState ts(wseq,wqv); //work with this structure from now on
do {
  int prev_t3=r.trim3;
  int prev_t5=r.trim5;
  trim_code=0;
  if (ts.w3upd) {
    if (trim_poly3(ts.wseq, ts.w5, ts.w3, polyA_seed)) {
      trim_code='A';
      STrimOp trimop(3, trim_code, (ts.w5+(ts.wseq.length()-1-ts.w3)));
      #ifdef TRIMDEBUG
        GMessage("#DBG# 3' polyA trimming %d bases\n",trimop.tlen);
      #endif
      r.trimhist.Add(trimop);
      b_trimA+=trimop.tlen;
      if (!trimmedA) { num_trimA++; trimmedA=true; }
    }
    else
    if (polyBothEnds && trim_poly3(ts.wseq, ts.w5, ts.w3, polyT_seed)) {
      trim_code='T';
      STrimOp trimop(3, trim_code, (ts.w5+(ts.wseq.length()-1-ts.w3)));
     #ifdef TRIMDEBUG
       GMessage("#DBG# 3' polyT trimming %d bases\n",trimop.tlen);
     #endif
      r.trimhist.Add(trimop);
      b_trimT+=trimop.tlen;
      if (!trimmedT) { num_trimT++; trimmedT=true; }
    }
    if (trim_code) {
      ts.wupd=true;
      if (ts.update(trim_code, r.trim5, r.trim3))
         return trim_code;
      trim_code=0;
    }
   }
   int tidx=-1;
   if (ts.wupd && trim_adapter3(ts.wseq, ts.w5, ts.w3, tidx)) {
       if (showAdapterIdx && tidx>=0) trim_code=('a'+tidx);
         else trim_code='V';
       STrimOp trimop(3, trim_code, (ts.w5+(ts.wseq.length()-1-ts.w3)));
       #ifdef TRIMDEBUG
          GMessage("#DBG# 3' adapter trimming %d bases\n",trimop.tlen);
       #endif

       r.trimhist.Add(trimop);
       b_trimV+=trimop.tlen;
       if (!trimmedV) { num_trimV++; trimmedV=true; }
   }
   if (trim_code) {
    if (ts.update(trim_code, r.trim5, r.trim3))
        return trim_code;
    //wseq, w5, w3 were updated, let this fall through to next check
    trim_code=0;
   }
   if (ts.w5upd) {
    if (trim_poly5(ts.wseq, ts.w5, ts.w3, polyT_seed)) {
        trim_code='T';
        STrimOp trimop(5, trim_code,(ts.w5+(ts.wseq.length()-1-ts.w3)));
        #ifdef TRIMDEBUG
          GMessage("#DBG# 5' polyT trimming %d bases\n",trimop.tlen);
        #endif
        r.trimhist.Add(trimop);
        b_trimT+=trimop.tlen;
        if (!trimmedT) { num_trimT++; trimmedT=true; }
    }
    else
    if (polyBothEnds && trim_poly5(ts.wseq, ts.w5, ts.w3, polyA_seed)) {
        trim_code='A';
        STrimOp trimop(5, trim_code,(ts.w5+(ts.wseq.length()-1-ts.w3)));
		#ifdef TRIMDEBUG
		  GMessage("#DBG# 5' polyA trimming %d bases\n",trimop.tlen);
		#endif
        r.trimhist.Add(trimop);
        b_trimA+=trimop.tlen;
        if (!trimmedA) { num_trimA++; trimmedA=true; }
    }
    if (trim_code) {
      ts.wupd=true;
      if (ts.update(trim_code, r.trim5, r.trim3))
         return trim_code;
      trim_code=0;
    }
   }
   tidx=-1;
   if (ts.wupd && trim_adapter5(ts.wseq, ts.w5, ts.w3, tidx)) {
      if (showAdapterIdx && tidx>=0) trim_code=('a'+tidx);
   	   else trim_code='V';
      STrimOp trimop(5, trim_code,(ts.w5+(ts.wseq.length()-1-ts.w3)));
	  #ifdef TRIMDEBUG
	    GMessage("#DBG# 5' adapter trimming %d bases\n",trimop.tlen);
	  #endif
      r.trimhist.Add(trimop);
      b_trimV+=trimop.tlen;
      if (!trimmedV) { num_trimV++; trimmedV=true; }
      }
  //checked the 3' end
  if (trim_code) {
    if (ts.update(trim_code, r.trim5, r.trim3))
        return trim_code;
    //wseq, w5, w3 were updated, let this fall through to next check
    trim_code=0;
  }
  ts.w3upd=(r.trim3!=prev_t3);
  ts.w5upd=(r.trim5!=prev_t5);
  ts.wupd=(ts.w3upd || ts.w5upd);
} while (ts.wupd);
if (doCollapse) {
   //keep read for later
   FqDupRec* dr=dhash.Find(ts.wseq.chars());
   if (dr==NULL) { //new entry
          //if (prefix.is_empty())
             dhash.Add(ts.wseq.chars(),
                  new FqDupRec(&ts.wqv, r.rid.chars()));
          //else dhash.Add(wseq.chars(), new FqDupRec(wqv.chars(),wqv.length()));
         }
      else
         dr->add(ts.wqv);
   } //collapsing duplicates
 else { //not collapsing duplicates
   //apply the dust filter now
   if (doDust) {
     int dustbases=dust(ts.wseq);
     if (dustbases>(ts.wseq.length()>>1)) {
        return 'D';//trash code
        }
     }
   } //not collapsing duplicates
return (r.trim5>0 || r.trim3>0) ? 1 : 0;
}

void printHeader(FILE* f_out, char recmarker, RData& rd) { //GStr& rname, GStr& rinfo) {
 //GMessage("printing Header..%c%s\n",recmarker, rname.chars());
 fprintf(f_out, "%c%s",recmarker, rd.rid.chars());
 if (trimInfo) 
    fprintf(f_out, " %d %d", rd.trim5, rd.trim3);
 if (!rd.rinfo.is_empty())
    fprintf(f_out, " %s", rd.rinfo.chars());
 fprintf(f_out, "\n");
 }

void write1Read(FILE* fout, RData& rd, int counter) {
  //GStr& rname, GStr& rinfo, GStr& rseq, GStr& rqv,
  GStr seq=rd.getTrimSeq();
  GStr qv=rd.getTrimQv();
  if (seq.is_empty()) {
     seq="A";
     qv="B";
  }
  bool asFasta=(rd.qv.is_empty() || fastaOutput);
  if (asFasta) {
   if (prefix.is_empty()) {
      printHeader(fout, '>', rd);
      //fprintf(fout, "%s\n", rd.seq.chars()); //plain one-line fasta for now
      writeFasta(fout, NULL, NULL, seq.chars(), 100, seq.length());
      }
     else {
      fprintf(fout, ">%s_%08d",prefix.chars(), counter);
      if (trimInfo) 
        fprintf(fout," %d %d", rd.trim5, rd.trim3);
      //fprintf(fout, "\n%s\n", rd.seq.chars());
      writeFasta(fout, NULL, NULL, seq.chars(), 100, seq.length());
      }
    }
  else {  //fastq
   if (convert_phred) convertPhred(qv);
   if (prefix.is_empty()) {
      printHeader(fout, '@', rd);
      fprintf(fout, "%s\n+\n%s\n", seq.chars(), qv.chars());
      }
     else {
      fprintf(fout, "@%s_%08d", prefix.chars(), counter);
      if (trimInfo) 
        fprintf(fout," %d %d", rd.trim5, rd.trim3);
      fprintf(fout,"\n%s\n+\n%s\n", seq.chars(), qv.chars() );
      }
    }
}

void CTrimHandler::writeRead(RData& rd, RData& rd2) {
#ifndef NOTHREADS
	GLockGuard<GFastMutex> guard(writeMutex);
#endif

	if (show_Trim) { outcounter++; return; }
	bool writePair=false;
	if (pairedOutput && (rd.trashcode<=1 || rd2.trashcode<=1))
		 writePair=true;
	if (rinfo->f_out && (writePair || rd.trashcode<=1)) {
		outcounter++;
		write1Read(rinfo->f_out, rd, outcounter);
	}
	if (rinfo->f_out2 && (writePair || rd2.trashcode<=1))  {
		if (!pairedOutput) outcounter++;
		write1Read(rinfo->f_out2, rd2, outcounter);
	}
}

//trim_report(char trimcode, GStr& rname, GVec<STrimOp>& t_hist, FILE* frep)
void CTrimHandler::trim_report(RData& r, int mate) {
	if (freport && r.trashcode) {
#ifndef NOTHREADS
		GLockGuard<GFastMutex> guard(reportMutex);
#endif
		GStr rname(r.rid);
		if (rinfo && rinfo->fq2) {
			if (mate) {
				if (!rname.endsWith("/2")) rname+="/2";
			}
			else {
				if (!rname.endsWith("/1")) rname+="/1";
			}
		}
		if (r.trimhist.Count()==0) {
			if (r.trashcode<=' ') r.trashcode='?';
			fprintf(freport, "%s\t%c\t%c\n",rname.chars(), r.trashcode, r.trashcode);
			return;
		}
		fprintf(freport, "%s\t", rname.chars());
		for (int i=0;i<r.trimhist.Count();i++) {
			fprintf(freport,"%d%c%d",r.trimhist[i].tend, r.trimhist[i].tcode, r.trimhist[i].tlen);
			if (i<r.trimhist.Count()-1) fprintf(freport,",");
		}
		if (r.trashcode>' ') fprintf(freport, "\t%c\n", r.trashcode);
		else fprintf(freport, "\t\n");
	}
}

GStr getFext(GStr& s, int* xpos=NULL) {
 //s must be a filename without a path
 GStr r("");
 if (xpos!=NULL) *xpos=0;
 if (s.is_empty() || s=="-") return r;
 int p=s.rindex('.');
 int d=s.rindex('/');
 if (p<=0 || p>s.length()-2 || p<s.length()-7 || p<d) return r;
 r=s.substr(p+1);
 if (xpos!=NULL) *xpos=p+1;
 r.lower();
 return r;
 }

void baseFileName(GStr& fname) {
 //remove all known extensions, like .txt,fq,fastq,fasta,fa)(.gz .gzip .bz2 .bzip2) .
 int xpos=0;
 GStr fext=getFext(fname, &xpos);
 if (xpos<=1) return;
 bool extdel=false;
 GStr f2;
 if (fext=="z" || fext=="zip") {
   extdel=true;
   }
  else if (fext.length()>=2) {
   f2=fext.substr(0,2);
   extdel=(f2=="gz" || f2=="bz");
   }
 if (extdel) {
   fname.cut(xpos-1);
   fext=getFext(fname, &xpos);
   if (xpos<=1) return;
   }
 extdel=false;
 if (fext=="f" || fext=="fq" || fext=="txt" || fext=="seq" || fext=="sequence") {
   extdel=true;
   }
  else if (fext.length()>=2) {
   extdel=(fext.substr(0,2)=="fa");
   }
 if (extdel) fname.cut(xpos-1);
 GStr fncp(fname);
 fncp.lower();
 fncp.chomp("seq");
 fncp.chomp("sequence");
 fncp.trimR("_.");
 if (fncp.length()<fname.length()) fname.cut(fncp.length());
}

FILE* prepOutFile(GStr& infname, GStr& pocmd) {
  FILE* f_out=NULL;
  GStr fname;
  bool fullname=(infname=="-");
  if (!fullname) {
       fname=getFileName(infname.chars());
       baseFileName(fname);
  }
  //eliminate known extensions
  if (outsuffix.is_empty() || outsuffix=="-") { return stdout; }
    else if (pocmd.is_empty()) {
               GStr oname(outdir);
               oname.append('/');
               if (fullname) {
                 oname.append(outsuffix);
               }
               else {
                 oname.append(fname);
                 oname.append('.');
                 oname.append(outsuffix);
               }
               f_out=fopen(oname.chars(),"w");
               if (f_out==NULL) GError("Error: cannot create file '%s'\n",oname.chars());
               }
            else {
              GStr oname(pocmd);
              oname.append('>');
              oname.append(outdir);
              oname.append('/');
              if (!fullname) {
                oname.append(fname);
                oname.append('.');
              }
              oname.append(outsuffix);
              //pocmd.append(oname);
              f_out=popen(oname.chars(), "w");
              if (f_out==NULL) GError("Error: cannot popen '%s'\n",oname.chars());
              }
 return f_out;
}

void guess_unzip(GStr& fname, GStr& picmd) {
 GStr fext=getFext(fname);
 if (fext=="gz" || fext=="gzip" || fext=="z") {
    picmd="gzip -cd ";
    }
   else if (fext=="bz2" || fext=="bzip2" || fext=="bz" || fext=="bzip") {
    picmd="bzip2 -cd ";
    }
}

void addAdapter(GPVec<CASeqData>& adapters, GStr& seq, GAlnTrimType trim_type) {
  if (seq.is_empty() || seq=="-" ||
      seq=="N/A" || seq==".") return;
 ++adapter_idx;
 CASeqData* adata = new CASeqData(revCompl, adapter_idx);
 int idx=adapters.Add(adata);
 if (idx<0) GError("Error: failed to add adapter!\n");
 adapters[idx]->trim_type=trim_type;
 adapters[idx]->update(seq.chars());
 if (trim_type==galn_TrimEither) {
  //special case, can only be used with adapters==adapters5
  //add to adapters3 automatically
  adapters3.Add(adata);
 }
 all_adapters.Add(adata);
}


int loadAdapters(const char* fname) {
  GLineReader lr(fname);
  char* l;
  while ((l=lr.nextLine())!=NULL) {
   if (lr.tlength()<=3 || l[0]=='#') continue;
   if ( l[0]==' ' || l[0]=='\t' || l[0]==',' ||
        l[0]==';'|| l[0]==':' ) { //starts with a delimiter
       //so we're reading 3' adapter here
      int i=1;
      while (l[i]!=0 && isspace(l[i])) {
        i++;
        }
      if (l[i]!=0) {
        GStr s(&(l[i]));
        addAdapter(adapters3, s, galn_TrimRight);
        continue;
        }
      }
    else {
      //5' adapter (at least)
      GStr s(l);
      char lastc=s[s.length()-1];
      s.startTokenize("\t ;,:");
      GStr a5,a3;
      if (s.nextToken(a5))
            s.nextToken(a3);
      else {
         //GMessage("No token found on adapter line\n");
         continue; //nothing on this line
         }
      bool nodelim=(lastc>='A');
      GAlnTrimType ttype5=galn_TrimLeft;
      //GMessage("tokens found: <%s> , <%s>\n",a5.chars(),a3.chars());
      a5.upper();
      a3.upper();
      if ((a3.is_empty() && nodelim) || a3==a5 || a3=="=") {
         a3.clear();
         ttype5=galn_TrimEither;
         }
      addAdapter(adapters5, a5, ttype5);
      addAdapter(adapters3, a3, galn_TrimRight);
      }
   }
   return adapters5.Count()+adapters3.Count();
}

void setupFiles(FILE*& f_in, FILE*& f_in2, FILE*& f_out, FILE*& f_out2,
                       GStr& s, GStr& infname, GStr& infname2) {
// uses outsuffix to generate output file names and open file handles as needed
 infname="";
 infname2="";
 f_in=NULL;
 f_in2=NULL;
 f_out=NULL;
 f_out2=NULL;
 //analyze outsuffix intent
 GStr pocmd;
 if (outsuffix=="-") {
    f_out=stdout;
    }
   else {
    GStr ox=getFext(outsuffix);
    if (ox.length()>2) ox=ox.substr(0,2);
    if (ox=="gz") pocmd="gzip -9 -c ";
        else if (ox=="bz") pocmd="bzip2 -9 -c ";
    }
 if (s=="-") {
    f_in=stdin;
    infname=s;
    f_out=prepOutFile(infname, pocmd);
    return;
    } // streaming from stdin
 s.startTokenize(",:");
 s.nextToken(infname);
 bool paired=s.nextToken(infname2);
 if (fileExists(infname.chars())==0)
    GError("Error: cannot find file %s!\n",infname.chars());
 GStr fname(getFileName(infname.chars()));
 GStr picmd;
 guess_unzip(fname, picmd);
 if (picmd.is_empty()) {
   f_in=fopen(infname.chars(), "r");
   if (f_in==NULL) GError("Error opening file '%s'!\n",infname.chars());
   }
  else {
   picmd.append(infname);
   f_in=popen(picmd.chars(), "r");
   if (f_in==NULL) GError("Error at popen %s!\n", picmd.chars());
   }
 if (f_out==stdout) {
   if (paired) GError("Error: output suffix required for paired reads\n");
   return;
   }
 f_out=prepOutFile(infname, pocmd);
 if (!paired) return;
 if (doCollapse) GError("Error: sorry, -C option cannot be used with paired reads!\n");
 // ---- paired reads:-------------
 if (fileExists(infname2.chars())==0)
     GError("Error: cannot find file %s!\n",infname2.chars());
 picmd="";
 GStr fname2(getFileName(infname2.chars()));
 guess_unzip(fname2, picmd);
 if (picmd.is_empty()) {
   f_in2=fopen(infname2.chars(), "r");
   if (f_in2==NULL) GError("Error opening file '%s'!\n",infname2.chars());
   }
  else {
   picmd.append(infname2);
   f_in2=popen(picmd.chars(), "r");
   if (f_in2==NULL) GError("Error at popen %s!\n", picmd.chars());
   }
 f_out2=prepOutFile(infname2, pocmd);
 pairedOutput=true;
}

#ifndef NOTHREADS
void workerThread(GThreadData& td) {
	CTrimHandler trimmer((RInfo*)td.udata);
	trimmer.processAll();
}
#endif

void CTrimHandler::processAll() {
	if (fetchReads()) {
		while (processRead()) {
			if (rbuf_p<=0) {
				flushReads(); //flush the read buffers
				if (!fetchReads()) break;
			}
		}
	}
}

void CTrimHandler::updateTrashCounts(RData& rd) {
	if (rd.trashcode>1) { //read/pair trashed
		if (rd.trashcode=='s') trash_s++;
		else if (rd.trashcode=='A' || rd.trashcode=='T') trash_poly++;
		else if (rd.trashcode=='Q') trash_Q++;
		else if (rd.trashcode=='N') trash_N++;
		else if (rd.trashcode=='D') trash_D++;
		else if (rd.trashcode=='V') trash_V++;
		else {
			trash_X++;
		}
	}

}

bool CTrimHandler::processRead() {
	//GStr rseq, rqv, seqid, seqinfo;
	//GStr rseq2, rqv2, seqid2, seqinfo2;
	RData* rd=NULL;
	RData* rd2=NULL;
	if (nextRead(rd, rd2)) {
		rd->trashcode=process_read(*rd);
		//trashcode: 0 if the read was not trimmed at all and it's long enough
		//       1 if it was just trimmed but survived,
		//       >1 (=trash code character ) if it was trashed for any reason
#ifdef TRIMDEBUG
		if (rd->trim5>0 || rd->trim3<rd->seq.length()-1) {
			char tc=(rd->trashcode>32)? rd->trashcode : ('0'+rd->trashcode);
			GMessage("####> Trim code [%c] ( trim5=%d, trim3=%d): \n",tc, rd->trim5,rd->trim3);
			showTrim(*rd);
		}
		else {
			GMessage("####> No trimming for this read.\n");
		}
#endif
		if (show_Trim) {
			showTrim(*rd);
		}
		if (rd->trim5>0) {
			b_trim5+=rd->trim5;
			num_trim5++;
		}
		if (rd->trim3>0) {
			b_trim3+=rd->trim3;
			num_trim3++;
		}
		if (rinfo->fq2!=NULL && rd2!=NULL) { //paired
			if (!disableMateNameCheck && rd->rid.length()>4 && rd2->rid.length()>=rd->rid.length()) {
				if (rd->rid.substr(0,rd->rid.length()-3)!=rd2->rid.substr(0,rd->rid.length()-3)) {
					GError("Error: no paired match for read %s vs %s (%s,%s)\n",
							rd->rid.chars(), rd2->rid.chars(), rinfo->infname.chars(), rinfo->infname2.chars());
				}
			}
			rd2->trashcode=process_read(*rd2);
			if (rd2->trim5>0) {
				b_trim5+=rd2->trim5;
				num_trim5++;
			}
			if (rd2->trim3>0) {
				b_trim3+=rd2->trim3;
				num_trim3++;
			}
			updateTrashCounts(*rd2);
		} //paired
		updateTrashCounts(*rd);
		return true;
	} //proceed read/pair
	return false;
}

