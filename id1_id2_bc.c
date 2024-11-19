# include <stdio.h>
// # define const float bla = 3.2;

double abs_value(double x)
{
    if (x >= 0.0)
        return x;
    else
        return -x;
}

int main() {

    int a, b;
    printf("enter the source base:\n");
    scanf("%d %d", &a, &b);
    printf("you entered %d %d\n" ,a, b);
    // printf("Hello, World! %f\n", bla);
    return 0;
}