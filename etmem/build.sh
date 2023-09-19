# cd ../
rm -rf build
mkdir build
cd build
cmake ..
make -j32
# cd build
# cd bin 
# ./etmemd -l 0 -s sock &
# pgrep etmemd
# ./etmem obj del -f /home/zpw/etmem/etmem/conf/slide_conf.yaml -s sock
# ./etmem obj add -f /home/zpw/etmem/etmem/conf/slide_conf.yaml -s sock
# ./etmem project start -n test -s sock
# ./etmem project stop -n test -s sock