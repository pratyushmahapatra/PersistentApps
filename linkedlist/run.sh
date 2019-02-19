#for i in {1..5}
#do
#    rm -f *.txt
#    ./linkedlist_persistent | grep "Program\|Number" >> persistent.log
#done
#
#for i in {1..5}
#do
#    rm -f *.txt
#    ./linkedlist_pp | grep "Program\|Number" >> partlypersistent.log
#done
#
#for i in {1..5}
#do
#    rm -f *.txt
#    ./linkedlist_volatile_mmap | grep "Program" >> volatile_mmap.log
#done
#
#for i in {1..5}
#do
#    rm -f *.txt
#    ./linkedlist_volatile_malloc | grep "Program" >> volatile_malloc.log
#done
#
#for i in {1..5}
#do
#    rm -f *.txt
#    ./linkedlist_volatile_mallocall | grep "Program" >> volatile_mallocall.log
#done

for i in {1..5}
do
    rm -f *.txt
    ./linkedlist_persistent_msync | grep "Program\|Number" >> persistent_msync.log
done

for i in {1..5}
do
    rm -f *.txt
    ./linkedlist_pp_msync | grep "Program\|Number" >> pp_msync.log
done
