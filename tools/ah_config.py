allQueues = [ 'bskfifo'
             , 'ebstack'
             , 'fc'
             , 'kstack'
             , 'lb'
             , 'lcrq'
             , 'ms'
             , 'rd'
             , 'scal1random'
             , 'scal2random'
             , 'scal1rr'
             , 'scal2rr'
             , 'scal120rr'
             , 'sq'
             , 'tsstack'
             , 'tstack'
             , 'uskfifo'
             , 'wf'
             , 'tsstutterlist' 
             , 'tsstutterarray'
             , 'tsatomiclist'  
             , 'tsatomicarray' 
             , 'tshwlist'      
             , 'tshwarray'     
             , 'tshw2ts'     
             , 'tsatomic2ts'     
             , 'tsstutter2ts'     
             , 'tsqueuestutter' 
             , 'tsqueueatomic' 
             , 'tsqueuehw' 
             , 'tsqueue2ts' 
             , 'tsqueueatomic2ts' 
             , 'tsqueuestutter2ts' 
             , 'tsdequestutter' 
             , 'tsdequeatomic' 
             , 'tsdequehw' 
             , 'tsdequehw2ts' 
             , 'tsdequeatomic2ts' 
             , 'tsdequestutter2ts' 
             , 'tsdequeshw' 
             , 'tsdequeshw2ts' 
             , 'tsdequesatomic2ts' 
             , 'tsdequesstutter2ts' 
             , 'tsdequeqhw' 
             , 'tsdequeqhw2ts' 
             , 'tsdequeqstutter2ts' 
             , 'tsdequeqatomic2ts' 
             ]

executables = { 
               'scal1rr' : 'dq-partrr -partitions 1'
             , 'scal2rr' : 'dq-partrr -partitions 2'
             , 'scal120rr' : 'dq-partrr -partitions 120'
             , 'scal1random' : 'dq-1random -nohw_random'
             , 'scal2random' : 'dq-2random -nohw_random'
             , 'wf' : 'wf-ppopp11'
             , 'ebstack' : 'ebstack -delay 20000 -collision 12'#2000 -delay 20000 -collision 12' #250 -delay 13000 -collision 8'
             , 'tsstutterlist'   : 'tsstack -stutter_clock -list -noinit_threshold'
             , 'tsstutterarray'  : 'tsstack -stutter_clock -array -noinit_threshold'
             , 'tsatomiclist'    : 'tsstack -atomic_clock -list -init_threshold'
             , 'tsatomicarray'   : 'tsstack -atomic_clock -array -init_threshold'
             , 'tshwlist'        : 'tsstack -hwp_clock -list -init_threshold'
             , 'tshwarray'       : 'tsstack -hwp_clock -array -init_threshold'
             , 'tshw2ts'         : 'tsstack -hwp_clock -2ts -init_threshold'# -delay 7500' #250: 20000 2000: 7500
             , 'tsatomic2ts'         : 'tsstack -atomic_clock -2ts -init_threshold' # -delay 7500' #250: 20000 2000: 7500
             , 'tsstutter2ts'         : 'tsstack -stutter_clock -2ts -init_threshold' # -delay 7500' #250: 20000 2000: 7500
             , 'tsqueuestutter'  : 'tsqueue -stutter_clock -list'
             , 'tsqueueatomic'   : 'tsqueue -atomic_clock -list'
             , 'tsqueuehw'       : 'tsqueue -hwp_clock -list'
             , 'tsqueue2ts'      : 'tsqueue -hwp_clock -2ts'# -delay 15000' #250: 12500 2000: 15000
             , 'tsqueueatomic2ts'      : 'tsqueue -atomic_clock -2ts'#  -delay 17500' #250: 12500 2000: 17500
             , 'tsqueuestutter2ts'      : 'tsqueue -stutter_clock -2ts'#  -delay 17500' #250: 12500 2000: 17500
             , 'tsdequestutter'  : 'tsdeque -list -stutter_clock -init_threshold'
             , 'tsdequeatomic'   : 'tsdeque -list -atomic_clock -init_threshold'
             , 'tsdequehw'       : 'tsdeque -list -hwp_clock -init_threshold'
             , 'tsdequehw2ts'    : 'tsdeque -2ts -hwp_clock -init_threshold'# -delay 15000' #250: 17500 2000: 15000
             , 'tsdequeatomic2ts'    : 'tsdeque -2ts -atomic_clock -init_threshold'# -delay 15000' #250: 17500 2000: 15000
             , 'tsdequestutter2ts'    : 'tsdeque -2ts -stutter_clock -init_threshold'# -delay 15000' #250: 17500 2000: 15000
             , 'tsdequeshw'      : 'tsdeques -list -hwp_clock -init_threshold'
             , 'tsdequeshw2ts'   : 'tsdeques -2ts -hwp_clock -init_threshold'# -delay 10000' #250: 15000 2000: 10000 
             , 'tsdequesatomic2ts'   : 'tsdeques -2ts -atomic_clock -init_threshold'# -delay 10000' #250: 15000 2000: 10000 
             , 'tsdequesstutter2ts'   : 'tsdeques -2ts -stutter_clock -init_threshold'# -delay 10000' #250: 15000 2000: 10000 
             , 'tsdequeqhw'      : 'tsdequeq -list -hwp_clock -init_threshold'
             , 'tsdequeqhw2ts'   : 'tsdequeq -2ts -hwp_clock -init_threshold'# -delay 17500' #250: 10000 2000: 17250
             , 'tsdequeqatomic2ts'   : 'tsdequeq -2ts -atomic_clock -init_threshold'# -delay 25000' #250: 25000 2000: 25000
             , 'tsdequeqstutter2ts'   : 'tsdequeq -2ts -stutter_clock -init_threshold'# -delay 25000' #250: 25000 2000: 25000
             , 'tsdequeqstutter' : 'tsdequeq -list -stutter_clock -init_threshold'
             , 'tsdequeqatomic'  : 'tsdequeq -list -atomic_clock -init_threshold'
             , 'tsdequedhw'      : 'tsdequed -list -hwp_clock -init_threshold'
             , 'tsdequedhw2ts'   : 'tsdequed -2ts -hwp_clock -init_threshold'# -delay 15000' #250: 25000 2000: 15000
             , 'tsdequedatomic2ts'   : 'tsdequed -2ts -atomic_clock -init_threshold'# -delay 15000' #250: 25000 2000: 15000
             , 'tsdequedstutter2ts'   : 'tsdequed -2ts -stutter_clock -init_threshold'# -delay 15000' #250: 25000 2000: 15000
             , 'lcrq': 'lcrq -noreuse_memory'
             }

#hasPartials = ['scal2random', 'scalrr', 'uskfifo', 'bskfifo', 'scal1random', 'scaltlrr', 'sq', 'rd']

pParameter = {'scal2random': 'p'
             , 'scal1random' : 'p'
             , 'scalrr': 'p'
             , 'scalid': 'p'
             , 'scaltlrr' : 'p'
             , 'uskfifo' : 'k' 
             , 'bskfifo' : 'k'
             , 'kstack' : 'k'
             , 'sq' : 'k'
             , 'rd' : 'quasi_factor'
             , 'dq1random': 'p'
             , 'dq2random': 'p'
             , 'dqid': 'p'
             , 'dqrr': 'p'
             , 'dqtlrr': 'p'
             , 'dqh1random' : 'p'
             , 'dqh2random' : 'p'
             , 'scalperm' : 'p'
             , 'scal1rr' : 'p'
             , 'scal2rr' : 'p'
             , 'scal4rr' : 'p'
             , 'scal8rr' : 'p'
             , 'scal20rr' : 'p'
             , 'scal40rr' : 'p'
             , 'scal80rr' : 'p'
             , 'scal120rr' : 'p'
             }

works = [0, 1000, 2000, 4000, 8000, 16000, 32000, 64000]
allWorks = [0, 1000, 2000, 4000, 8000, 16000, 32000, 64000]

threads = [1, 2, 12, 24]

threadsB6 = [1, 2, 4, 8]
threadsB7 = [1, 2, 12, 24]
threadsB8 = [1, 2, 10, 20, 40, 80]

maxThreads=24
maxThreadsB6= 8
maxThreadsB8=80
maxThreadsB7=24

# The @ indicates the beginning of the real template. Everything before
# the @ will be cut away. This is necessary because all templates need to
# have the same placeholders.
templates = {
  'prodcon': '@../prodcon-{exe} -producers {thread} ' 
  + '-consumers {thread} -operations 1000000 -c {work} -prealloc_size 500m '
  + '{partials_param} {partials} {perfParam} -noset_rt_priority '
  + '> {filename}'

  , 'lcrq': '@../prodcon-{exe} -producers {thread} ' 
  + '-consumers {thread} -operations 1000000 -c {work} -disable_tl_allocator '
  + '{partials_param} {partials} {perfParam} -noset_rt_priority '
  + '> {filename}'

  , 'delay250': '@../prodcon-{exe} -producers {thread} -consumers {thread} '
  + '-operations 1000000 -c 250 {partials_param} {partials} -delay {work} '
  + '{perfParam} -noset_rt_priority -prealloc_size 500m > {filename}'

  , 'delay2000': '@../prodcon-{exe} -producers {thread} -consumers {thread} '
  + '-operations 1000000 -c 2000 {partials_param} {partials} -delay {work} '
  + '{perfParam} -noset_rt_priority -prealloc_size 500m > {filename}'

  , 'enq': '@../prodcon-{exe} -producers {thread} -consumers 0 '
  + '-operations 1000000 -c {work} {partials_param} {partials} '
  + '{perfParam} -noset_rt_priority -prealloc_size 1g > {filename}'

  , 'infprod': '@../prodcon-{exe} -producers {thread} -consumers {thread} '
  + '-operations 150000 -measure_at 10000 -c {work} '
  + '{partials_param} {partials} '
  + '{perfParam} -noset_rt_priority -prealloc_size 1g > {filename}'

  , 'deq': '@../prodcon-{exe} -producers {thread} -consumers {thread} '
  + '-operations 1000000 -c {work} {partials_param} {partials} -barrier '
  + '{perfParam} -noset_rt_priority -prealloc_size 1g > {filename}'

  , 'seqalt' : '@../seqalt-{exe} -allow_empty_returns -threads {thread} '
  + '-elements 1000000 -c {work} {partials_param} {partials} {perfParam} '
  + '-noset_rt_priority -prealloc_size 500m > {filename}'

  , 'shortest-path' : '{work}@../shortest-path-{exe} -threads {thread} '
  + '-height 100 -width 10000 {partials_param} {partials} '
  + '-noset_rt_priority -prealloc_size 1g > {filename}' 
}

filenameTemplates = {
    'prodcon': '@{directory}{queue}-t{thread}{partials_param}{partials}'
    + '-c{work}.txt'

    , 'lcrq': '@{directory}{queue}-t{thread}{partials_param}{partials}'
    + '-c{work}.txt'

    , 'enq': '@{directory}{queue}-t{thread}{partials_param}{partials}'
    + '-c{work}.txt'

    , 'delay250': '@{directory}{queue}-t{thread}{partials_param}{partials}'
    + '-c{work}.txt'

    , 'delay2000': '@{directory}{queue}-t{thread}{partials_param}{partials}'
    + '-c{work}.txt'

    , 'infprod': '@{directory}{queue}-t{thread}{partials_param}{partials}'
    + '-c{work}.txt'

    , 'deq': '@{directory}{queue}-t{thread}{partials_param}{partials}'
    + '-c{work}.txt'

    , 'seqalt': '@{directory}{queue}-t{thread}{partials_param}{partials}'
    + '-c{work}.txt'

    , 'shortest-path': '{work}@{directory}{queue}-t{thread}'
    + '{partials_param}{partials}.txt'
    }
