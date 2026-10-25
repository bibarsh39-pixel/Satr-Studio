#include <cstdio>
#include <iostream>

int main()
{
    // جرّب كتابة print في سطر جديد لاقتراح printf
    std::printf("Hello from Satr Studio!\n");

    std::cout << "Enter a number: ";
    int number = 0;
    std::cin >> number;

    std::printf("Double = %d\n", number * 2);
    return 0;
}
