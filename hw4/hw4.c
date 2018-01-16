#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <string.h>

#define TESTSIZE 25008
#define DATASIZE 25150
#define BATCHSIZE 2048
#define FEATSIZE 33
#define CHARBUFFERSIZE 150

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
int TREENUM = 100, THREADNUM = 2;

int cmp_feat(const void *a, const void *b){
    int feat = ((IdxFt*)a)->ftidx;
    int ida = ((IdxFt*)a)->idx;
    int idb = ((IdxFt*)b)->idx;
    if (X[ida][feat] > X[idb][feat]) return  1;
    if (X[ida][feat] < X[idb][feat]) return -1;
    return 0;
}

Node* newNode(void){
    Node* node = malloc(sizeof(Node));
    *node = (Node){-1, -1, NULL, NULL};
    return node;
}

Node* split(IdxFt* indices, int size, Info parent){

    assert(size != 0);
    if (size == 0) return NULL;

    Node* root = newNode();
    if (root == NULL){
        fprintf(stderr, "DANGER\n");
    }
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
            if (curr_loss < feat_loss){
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

    root->feat = best_feat;
    root->threshold = (X[tmpary[all_feat_slice-1].idx][best_feat] + X[tmpary[all_feat_slice].idx][best_feat]) / 2;

    root->left = split(tmpary, all_feat_slice, left);
    root->right = split(tmpary+all_feat_slice, size-all_feat_slice, right);
    return root;
}

int predict(Node* root, double* xt){
    while(root->feat != -1){
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

    return root;
}

int main(int argc, char **argv){

    // argparser
    char data_dir[CHARBUFFERSIZE] = {};
    char train_name[CHARBUFFERSIZE] = {};
    char test_name[CHARBUFFERSIZE] = {};
    char output_name[CHARBUFFERSIZE] = {};

    assert(strcmp("-data", argv[1]) == 0);
    strcpy(data_dir, argv[2]);
    sprintf(train_name, "%s/training_data", data_dir);
    sprintf(test_name, "%s/testing_data", data_dir);

    assert(strcmp("-output", argv[3]) == 0);
    strcpy(output_name, argv[4]);

    assert(strcmp("-tree", argv[5]) == 0);
    TREENUM = atoi(argv[6]);

    assert(strcmp("-thread", argv[7]) == 0);
    THREADNUM = atoi(argv[8]);

    srand(7122);

    // read training data
    FILE* train = fopen(train_name, "r");
    for (int i = 0; i != DATASIZE; ++i){
        int _; fscanf(train, "%d", &_);
        for (int j = 0; j != FEATSIZE; ++j)
            fscanf(train, "%lf", &X[i][j]);
        fscanf(train, "%d", &Y[i]);
    }
    fclose(train);

    // build random forest
    Node* forest[TREENUM];
    for (int i = 0; i != TREENUM; ++i)
        forest[i] = build();

    // read testing data
    FILE* test = fopen(test_name, "r");
    for (int i = 0; i != TESTSIZE; ++i){
        int _; fscanf(test, "%d", &_);
        for (int j = 0; j != FEATSIZE; ++j)
            fscanf(test, "%lf", &Xt[i][j]);
    }
    fclose(test);

    // write output data
    FILE* output = fopen(output_name, "w+");
    fprintf(output, "id,label\n");
    for (int i = 0; i != TESTSIZE; ++i){
        double res = 0;
        for (int t = 0; t != TREENUM; ++t)
            res += predict(forest[t], Xt[i]);
        if (res / TREENUM > 0.5f)
            fprintf(output, "%d,1\n", i);
        else
            fprintf(output, "%d,0\n", i);
    }
    fclose(output);

    return 0;
}