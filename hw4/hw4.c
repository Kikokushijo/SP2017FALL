#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <time.h>
#include <string.h>
// #define DATASIZE 20000
#define TESTSIZE 25008
#define DATASIZE 25150
#define BATCHSIZE 1024
#define FEATSIZE 33
#define TREENUM 20

typedef struct node Node;
typedef struct info Info;
typedef struct idx_ft IdxFt;

struct node{
    int feat;
    double threshold;
    Node *left, *right;
};

struct info{
    int label[2];
};

struct idx_ft{
    int idx, ftidx;
};

double X[DATASIZE][FEATSIZE], Xt[TESTSIZE][FEATSIZE];
int Y[DATASIZE];
int zeros, ones;

int cmp_feat(const void *a, const void *b){
    int feat = ((IdxFt*)a)->ftidx;
    int ida = ((IdxFt*)a)->idx;
    int idb = ((IdxFt*)b)->idx;
    if (X[ida][feat] > X[idb][feat] + 1e-6) return  1;
    if (X[ida][feat] < X[idb][feat] - 1e-6) return -1;
    return 0;
}

Node* newNode(void){
    Node* node = malloc(sizeof(Node));
    *node = (Node){-1, -1, NULL, NULL};
    return node;
}

Node* split(IdxFt* indices, int size, Info parent){

    // for (int i = 0; i != size; ++i)
    //     fprintf(stderr, "%d%c", indices[i].idx, " \n"[i==size-1]);
    // fprintf(stderr, "zero: %d, one: %d\n", parent.label[0], parent.label[1]);

    assert(size != 0);
    if (size == 0) return NULL;

    Node* root = newNode();
    if (parent.label[0] == 0 || parent.label[1] == 0){
        assert(parent.label[0] >= 1 || parent.label[1] >= 1);
        root->threshold = (parent.label[0] == 0)? 1.0f:0.0f;
        return root;
    }

    IdxFt ary[size], tmpary[size];
    for (int i = 0; i != size; ++i)
        ary[i].idx = indices[i].idx;


    int loss = 2147483647, all_feat_slice = -1, best_feat = -1;
    Info arcd = {};
    for (int i = 0; i != FEATSIZE; ++i){

        for (int j = 0; j != size; ++j)
            ary[j].ftidx = i;

        qsort(ary, size, sizeof(IdxFt), cmp_feat);

        Info left_feat = {}, rcd = {};
        int feat_loss = 2147483647, slice;
        for (int j = 0; j < size - 1; ++j){
            left_feat.label[Y[ary[j].idx]]++;
            int curr_loss = (left_feat.label[0] * left_feat.label[1]) + 
                            ((parent.label[0] - left_feat.label[0]) *
                             (parent.label[1] - left_feat.label[1]));
            if (curr_loss < feat_loss && X[ary[j+1].idx][i] - X[ary[j].idx][i] > 1e-7){
                slice = j + 1;
                rcd = left_feat;
                feat_loss = curr_loss;
            }
        }

        if (feat_loss < loss){
            arcd = rcd;
            all_feat_slice = slice;
            best_feat = i;
            loss = feat_loss;
            memcpy(tmpary, ary, sizeof(IdxFt)*size);
        }
    }

    Info left = {}, right = {};
    for (int i = 0; i != all_feat_slice; ++i)
        left.label[Y[tmpary[i].idx]]++;
    for (int i = all_feat_slice; i != size; ++i)
        right.label[Y[tmpary[i].idx]]++;

    assert(left.label[0] + right.label[0] == parent.label[0]);
    assert(left.label[1] + right.label[1] == parent.label[1]);
    assert(left.label[0] == arcd.label[0]);
    assert(left.label[1] == arcd.label[1]);

    // puts("PASS2!");
    // printf("%d %lf\n", best_feat, (X[all_feat_slice][best_feat] + X[all_feat_slice+1][best_feat]) / 2);
    root->feat = best_feat;
    root->threshold = (X[tmpary[all_feat_slice-1].idx][best_feat] + X[tmpary[all_feat_slice].idx][best_feat]) / 2;
    // fprintf(stderr, "%d %lf\n", root->feat, root->threshold);
    // for (int i = 0; i != size; ++i)
    //     fprintf(stderr, "%d %f\n", tmpary[i].idx, X[tmpary[i].idx][best_feat]);

    // puts("PASS3!");
    // fprintf(stderr, "Slice: %d ", all_feat_slice);
    // fprintf(stderr, "RECORD: LEFT: %d %d RIGHT: %d %d\n", arcd.label[0], arcd.label[1], 
    //                                              parent.label[0] - arcd.label[0],
    //                                              parent.label[1] - arcd.label[1]);

    root->left = split(tmpary, all_feat_slice, left);
    root->right = split(tmpary+all_feat_slice, size-all_feat_slice, right);
    return root;
}

int predict(Node* root, double* xt){
    // fprintf(stderr, "Start predict... \n");
    while(root->feat != -1){
        // fprintf(stderr, "FEATID:%d THRESHOLD:%f FEATV:%f\n", root->feat, root->threshold, xt[root->feat]);
        if (xt[root->feat] > root->threshold + 1e-7)
            root = root->right;
        else
            root = root->left;
    }
    return (int)root->threshold;
}

Node* build(){
    IdxFt indices[BATCHSIZE] = {};
    Info parent = {};
    for (int i = 0; i != BATCHSIZE; ++i){
        indices[i].idx = rand() % DATASIZE;
        parent.label[Y[indices[i].idx]]++;
    }
    Node* root = split(indices, BATCHSIZE, parent);

    for (int i = 0; i != BATCHSIZE; ++i){
        int res = predict(root, X[indices[i].idx]);
        // fprintf(stderr, "%d %d %d\n", indices[i].idx, res, Y[indices[i].idx]);
        // assert(res == Y[indices[i].idx]);
    }

    return root;
}



int main(){

    srand(0);
    FILE* train = fopen("../data/training_data", "r");
    Info parent = {};
    for (int i = 0; i != DATASIZE; ++i){
        int _; fscanf(train, "%d", &_);
        for (int j = 0; j != FEATSIZE; ++j)
            fscanf(train, "%lf", &X[i][j]);
        fscanf(train, "%d", &Y[i]);
        parent.label[Y[i]]++;
    }
    fclose(train);

    zeros = parent.label[0], ones = parent.label[1];

    Node* forest[TREENUM] = {};
    for (int i = 0; i != TREENUM; ++i)
        forest[i] = build();


    FILE* test = fopen("../data/testing_data", "r");
    for (int i = 0; i != TESTSIZE; ++i){
        int _; fscanf(test, "%d", &_);
        for (int j = 0; j != FEATSIZE; ++j)
            fscanf(test, "%lf", &Xt[i][j]);
    }
    fclose(test);

    puts("id,label");
    for (int i = 0; i != TESTSIZE; ++i){
        double res = 0;
        for (int t = 0; t != TREENUM; ++t){
            res += predict(forest[t], Xt[i]);
        }
        if (res / TREENUM > 0.5f)
            printf("%d,1\n", i);
        else
            printf("%d,0\n", i);
    }
    return 0;
}