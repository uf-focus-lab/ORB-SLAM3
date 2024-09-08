## ORB_Vocabulary.bin

This file contains the default vocabulary for ORB_SLAM3.
It's content is N repeated entries each following the format below:

```c++
// ALL DATA IS LITTLE ENDIAN
struct list_entry {
    uint32_t id;
    uint8_t data[33];
    double weight;
};

sizeof(struct list_entry); // evaluates to 45
```
