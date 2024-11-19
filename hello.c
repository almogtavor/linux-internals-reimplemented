# include <stdio.h>
# define const float bla = 3.2;

int main() {
   float bla = 3.2;
   int flag = 0;      // initialize flag
   flag = 1;          // set flag
   if(0) {         // check flag
      printf("almog");
   }
   char c = 'a';
   printf("%d  %x  %o\n", 19, 15, 19);     // Output ?
   printf("%d  %x  %o\n", 0x1c, 0x1c, 0x1c); // Output ?
   printf("%d  %x  %o\n", 017, 017, 017);    // Output ?
   printf("%d\n", 11 + 0x11 + 011);          // Output ?
   printf("%x\n", 2097151);                  // Output ?
   printf("%d\n", 0x1FfFFf);
   unsigned long z = 8;
   signed long k = -900;
   printf("%ld\n", z +k);                 // Output ?
   printf("%lu\n", z +k);                 // Output ?
   printf("%d\n", 0x1FfFFf);                 // Output ?
   // printf("Hello, World! %f\n", bla);
   return 0;
}