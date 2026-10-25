#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "values.h"
typedef struct Node { int value; struct Node *next; } Node;
static Node *merge(Node *a, Node *b) {
    Node root={0,NULL}, *tail=&root;
    while(a && b) { Node **p=a->value < b->value ? &a : &b; tail->next=*p; *p=(*p)->next; tail=tail->next; }
    tail->next=a?a:b; return root.next;
}
static Node *sort(Node *head) {
    if(!head || !head->next)return head;
    Node *slow=head,*fast=head->next;
    while(fast && fast->next){slow=slow->next;fast=fast->next->next;}
    Node *other=slow->next;slow->next=NULL;return merge(sort(head),sort(other));
}
int main(void) {
    Node *head=NULL;
    for(int i=0;i<20000;i++){Node *n=malloc(sizeof(*n));assert(n);n->value=(i*7919)%20000;n->next=head;head=n;}
    head=sort(head);long long sum=0;int previous=-1,count=0;
    while(head){assert(head->value>previous);previous=head->value;sum+=head->value;count++;Node *next=head->next;free(head);head=next;}
    assert(count==20000 && sum==199990000);
    FILE *f=fopen("c-result.bin","wb");assert(f);assert(fwrite(&sum,sizeof(sum),1,f)==1);fclose(f);
    long long read=0;f=fopen("c-result.bin","rb");assert(f);assert(fread(&read,sizeof(read),1,f)==1);fclose(f);assert(read==sum);
    printf("C_OK:%lld\n",sum*MULTIPLIER);return 0;
}
