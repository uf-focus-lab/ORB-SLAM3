## ORB_Vocabulary.bin

This file contains the default vocabulary for ORB_SLAM3.
It's content is N repeated entries each following the format below:

```c++
struct list_entry {
    uint16_t id;
    uint8_t data[33];
    double weight;
};

sizeof(struct list_entry); // evaluates to 42
```
