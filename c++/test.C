int func()
{
    static int counter = 0;
    return counter++;
}

int main()
{
    while(int i=func(); i<10);
    cerr<<"Hello"<<endl;
    return 0;
}
