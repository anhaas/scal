benchmarks='delay250 delay2000'
#benchmarks='prodcon'

for i in 1 2 3 4 5
#for i in 1 
do 
#  for b in prodcon # seqalt # enq deq # infprod
  for b in $benchmarks
  do 
    python b8_benchmark.py ${b} /home/ahaas/pldi14/b8/results1m/${b}${i}/  
  done 
done
for b in $benchmarks
do 
#  python calc_average.py /home/ahaas/pldi14/b8/results1m/${b}/ /home/ahaas/pldi14/b8/results1m/${b}1/
  python calc_average.py /home/ahaas/pldi14/b8/results1m/${b}/ /home/ahaas/pldi14/b8/results1m/${b}1/ /home/ahaas/pldi14/b8/results1m/${b}2/ /home/ahaas/pldi14/b8/results1m/${b}3/ /home/ahaas/pldi14/b8/results1m/${b}4/ /home/ahaas/pldi14/b8/results1m/${b}5/  
#  python make_data_files_threads.py /home/ahaas/pldi14/b8/results1m/${b}/  
  python make_data_files_load.py /home/ahaas/pldi4/b8/results1m/${b}/  
done 
