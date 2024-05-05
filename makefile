all: oss user

oss: oss.cpp
        g++ -o oss oss.cpp

user: user.cpp
        g++ -o user user.cpp

clean:
        rm -f oss user
