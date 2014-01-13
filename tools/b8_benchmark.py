import ah_config;
import benchmark;
import os.path;
import shutil;
import sys;

directory = 'tmp/'

if (len(sys.argv) < 2):
  print ("This script requires at least one parameter, namely the benchmark\n" +
    "you want to run, which is either prodcon or seqalt.")
  exit()

application = sys.argv[1]

if (len(sys.argv) >= 3):
  directory = sys.argv[2]

if (not directory.endswith('/')) :
  directory = directory + '/'

outputDir = directory
if (not directory.startswith('/')) :
  outputDir = '/mnt/local_homes/ahaas/{directory}'.format(directory = directory)

if not os.path.exists(outputDir):
  os.makedirs(outputDir)

#queues = ah_config.allQueues
queues = [
             'hc-ts-interval-queue'
             , 'hc-ts-interval-stack'
             , 'ts-interval-queue'
             , 'ts-interval-deque'
             , 'ts-interval-stack'
             ]
works = [0, 2500, 5000, 7500, 10000, 12500, 15000, 17500, 20000, 22500, 25000, 27500, 30000, 32500, 35000, 35700, 40000]
threads = [40]
#works = [250, 2000]
maxThreads = ah_config.maxThreadsB8
#threads = [1, 10, 20, 30, 40, 50, 60, 70, 80]

if application == "prodcon" or application == "infprod":
  threads = [1, 5, 10, 15, 20, 25, 30, 35, 40]

if application in ah_config.templates:
  benchmark.runBenchmark(
                     template = ah_config.templates[application],
                     filenameTemplate = ah_config.filenameTemplates[application],
                     queues = queues, 
                     works = works, 
                     threads = threads, 
                     maxThreads = maxThreads, 
                     directory = outputDir, 
                     prefill = 0, 
                     performance = True)
else:
  print "The benchmark {benchmark} does not exist".format(benchmark = sys.argv[1])

