#include <iostream>
#include <fstream>
#include <iomanip>
#include "pin.H"
#include <time.h>

struct timespec start, end;
#define BILLION 1000000000L

PIN_LOCK lock;
INT32 numThreads = 0;

uint64_t clock_avg_t = 0;

ofstream OutFile;

static  TLS_KEY tls_key;

// keep track of thread and mutex running time
class thread_data_t
{
  public:
    thread_data_t() : mtime(0), mcount(0) {}
    uint64_t mtime;
    struct timespec mstart, tstart, tend; 
    uint64_t mcount;
};

thread_data_t* get_tls(THREADID threadid)
{
    thread_data_t* tdata = 
          static_cast<thread_data_t*>(PIN_GetThreadData(tls_key, threadid));
    return tdata;
}

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "threadtime.out", "specify output file name");

INT32 Usage()
{
    cerr << "This tool prints the elapsed total, thread, and mutex time" << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

VOID BeforeLock( THREADID threadid )
{
    // OutFile << "Thread " << threadid << " is about to lock" << endl;    
    thread_data_t* tdata = get_tls(threadid);
    clock_gettime(CLOCK_MONOTONIC, &tdata->mstart);

}

VOID AfterLock( THREADID threadid )
{
    // OutFile << "Thread " << threadid << " is after lock" << endl;
    thread_data_t* tdata = get_tls(threadid);
    struct timespec mend;
    clock_gettime(CLOCK_MONOTONIC, &mend);
   
    tdata->mcount++;  
    tdata->mtime += (BILLION * (mend.tv_sec - tdata->mstart.tv_sec) + mend.tv_nsec - tdata->mstart.tv_nsec) - clock_avg_t;
}

// This routine is executed for each image.
VOID ImageLoad(IMG img, VOID *)
{
    RTN rtn = RTN_FindByName(img, "pthread_mutex_lock");
    
    if ( RTN_Valid( rtn ))
    {
        RTN_Open(rtn);
        
        RTN_InsertCall(rtn, IPOINT_BEFORE, AFUNPTR(BeforeLock),
                       IARG_THREAD_ID, IARG_END);

        RTN_InsertCall(rtn, IPOINT_AFTER, AFUNPTR(AfterLock),
                       IARG_THREAD_ID, IARG_END);


        RTN_Close(rtn);
    }
}

VOID Fini(INT32 code, VOID *v)
{
    // Write to a file since cout and cerr maybe closed by the application
    clock_gettime(CLOCK_MONOTONIC, &end);
    uint64_t total_elapsed = BILLION * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
    //OutFile << "Total elapsed time: " << total_elapsed << endl;
    uint64_t m_elapsed;

    OutFile << setw(7) <<  "Thread " << setw(15) << "% of runtime " << setw(21) << "% waiting for a lock" << endl;
    for (INT32 t = 0; t < numThreads; t++)
    {
        thread_data_t* tdata = get_tls(t);
        m_elapsed = BILLION * (tdata->tend.tv_sec - tdata->tstart.tv_sec) + tdata->tend.tv_nsec - tdata->tstart.tv_nsec;
       /* OutFile << "Thread : " << t << " elapsed time: " << m_elapsed << " (" << setprecision(2) <<  ((double) m_elapsed / (double)total_elapsed) * 100.00 << "%)" \
        << " lock time: " << tdata->mtime << " (" << setprecision(2) << ((double)tdata->mtime / (double)m_elapsed) * 100.00 << "%)" << endl;
    
       */       
       OutFile << setw(6) << t << setw(15) << setprecision(2) <<  ((double) m_elapsed / (double)total_elapsed) * 100.00 << setw(22) << setprecision(2) << ((double)tdata->mtime / (double)m_elapsed) * 100.00 <<endl;
    }
    OutFile.close();
}

VOID ThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    PIN_GetLock(&lock, threadid+1);
    numThreads++;
    PIN_ReleaseLock(&lock);

    thread_data_t* tdata = new thread_data_t;
    clock_gettime(CLOCK_MONOTONIC, &tdata->tstart);

    PIN_SetThreadData(tls_key, tdata, threadid);

}

VOID ThreadFini(THREADID threadid, const CONTEXT *ctxt, INT32 code, VOID *v)
{
    thread_data_t* tdata = get_tls(threadid);
    clock_gettime(CLOCK_MONOTONIC, &tdata->tend);
}

VOID calcAvgTimes()
{
    struct timespec tmp;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for(int i = 0; i<100; i++)
    {
       clock_gettime(CLOCK_MONOTONIC, &tmp);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);

    clock_avg_t = BILLION * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
    clock_avg_t = clock_avg_t / 100.0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for(int i = 0; i<100; i++)
    {
        uint64_t tmp = 0;
        tmp += (BILLION * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec) - clock_avg_t;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    uint64_t inst_avg_time = BILLION * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;	
    inst_avg_time = inst_avg_time / 100.0;

    clock_avg_t = 2 * clock_avg_t + inst_avg_time;
}
int main(int argc, char * argv[])
{
    //  Initialize pin
    if (PIN_Init(argc, argv)) return Usage();
    PIN_InitSymbols();
    
    OutFile.open(KnobOutputFile.Value().c_str());

    // Initialize the lock
    PIN_InitLock(&lock);

    // Obtain  a key for TLS storage.
    tls_key = PIN_CreateThreadDataKey(0);

    // Register ImageLoad to be called when each image is loaded.
    IMG_AddInstrumentFunction(ImageLoad, 0);
    
    // Register Analysis routines to be called when a thread begins/ends
    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddThreadFiniFunction(ThreadFini, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);
   
    //c
    calcAvgTimes();
 
    clock_gettime(CLOCK_MONOTONIC, &start);

    thread_data_t* tdata = new thread_data_t;
    clock_gettime(CLOCK_MONOTONIC, &tdata->tstart);

    // Start the program, never returns
    PIN_StartProgram();
                         
    return 0;
}

