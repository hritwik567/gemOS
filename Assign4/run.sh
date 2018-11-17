rm -rf disk.img mnt
mkdir mnt
dd if=/dev/zero of=disk.img bs=1M count=1024
make clean
make
./objfs mnt -o use_ino
cd test
make clean
make
./put
echo "--------------get-----------"
./get > getOut
echo "--------------get-----------"
# echo "--------------rename-----------"
# ./rename CS330\#\#\#1 CS330\#\#\#101
# echo "--------------rename-----------"
# echo "--------------delete-----------"
# ./delete CS330\#\#\#4
# ./delete CS330\#\#\#2
# ./delete CS330\#\#\#3
# echo "--------------delete-----------"
# echo "--------------get-----------"
# ./get >> getOut
# echo "--------------get-----------"
cd ..
# stat mnt/CS330\#\#\#1
# fusermount -u mnt
# echo "Unmounted"
# # echo "--------------log-----------"
# # cat objfs.log
# # echo "--------------log-----------"
# sleep 1
# ./objfs mnt -o use_ino
# cd test
# echo "--------------get-----------"
# ./get
# echo "--------------get-----------"
# cd ..
fusermount -u mnt
# rm -rf disk.img mnt
# echo "--------------log-----------"
# cat objfs.log
# echo "--------------log-----------"
