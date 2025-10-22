if [ ! -f ./openssl ]; then
    git clone https://github.com/openssl/openssl.git
    cd openssl
    git reset --hard 81f438d4f654143766f54ec865aab36bc61355cf
    cd ..
fi

if [ ! -f ./openssl-copier ]; then
    cp -r openssl openssl-copier
    cd openssl-copier
    git apply ../openssl.diff
    cd ..
fi

path=$(pwd)
cd openssl
./config --prefix=${path}/openssl-ins
make -j24
make install -j24
cd ..

cd openssl-copier
./config --prefix=${path}/openssl-copier-ins
make -j24
make install -j24
cd ..
