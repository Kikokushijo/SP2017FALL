#include <stdio.h>
#include <stdlib.h>
#define DATASIZE 25150
#define FEATSIZE 33

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

double X[DATASIZE][FEATSIZE];
int Y[DATASIZE];
int zeros, ones;

int cmp_feat(const void *a, const void *b){
	// puts("CMP");
	int feat = ((IdxFt*)a)->ftidx;
	int ida = ((IdxFt*)a)->idx;
	int idb = ((IdxFt*)b)->idx;
	// printf("FEAT: %d A: %d B: %d\n", feat, ida, idb);
	// printf("%lf v.s. %lf\n", X[ida][feat], X[idb][feat]);
	if (X[ida][feat] > X[idb][feat]) return  1;
	if (X[ida][feat] < X[idb][feat]) return -1;
	return 0;
}

Node* newNode(void){
	Node* node = malloc(sizeof(Node));
	*node = (Node){-1, -1, NULL, NULL};
	return node;
}

Node* build(IdxFt* indices, int size, Info parent){

	if (size == 0) return NULL;

	Node* root = newNode();
	// if (parent.label[0] == 0 || parent.label[1] == 0 || size < 1000){
	// 	root->threshold = Y[indices[0].idx];
	// 	return root;
	// }

	printf("SIZE: %d\n", size);
	if (size < 16 || parent.label[0] == 0 || parent.label[1] == 0){
		double zero_prob = ((double)parent.label[0]) / zeros;
		double one_prob = ((double)parent.label[1]) / ones;
		double sum_prob = zero_prob + one_prob;
		zero_prob /= sum_prob, one_prob /= sum_prob;
		root->threshold = one_prob / sum_prob;
		printf("ZERO:%lf ONE:%lf\n", zero_prob, one_prob);
		return root;
	}

	IdxFt ary[size];
	for (int i = 0; i != size; ++i)
		ary[i].idx = indices[i].idx;

	// puts("PASS1!");

	int loss = 2147483647, all_feat_slice = -1, best_feat = -1;
	Info arcd = {};
	for (int i = 0; i != FEATSIZE; ++i){

		for (int j = 0; j != size; ++j)
			ary[j].ftidx = i;

		qsort(ary, size, sizeof(IdxFt), cmp_feat);
		// puts("AFTER SORT");

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
		}
	}

	// puts("PASS2!");
	// printf("%d %lf\n", best_feat, (X[all_feat_slice][best_feat] + X[all_feat_slice+1][best_feat]) / 2);
	root->feat = best_feat;
	root->threshold = (X[all_feat_slice][best_feat] + X[all_feat_slice+1][best_feat]) / 2;

	// puts("PASS3!");
	printf("Slice: %d ", all_feat_slice);
	printf("RECORD: LEFT: %d %d RIGHT: %d %d\n", arcd.label[0], arcd.label[1], 
												  parent.label[0] - arcd.label[0],
												  parent.label[1] - arcd.label[1]);
	root->left = build(ary, all_feat_slice, arcd);
	root->right = build(ary+all_feat_slice, size-all_feat_slice, (Info){parent.label[0] - arcd.label[0], 
														 parent.label[1] - arcd.label[1]});
	return root;
}

int main(){

	Info parent = {};
	for (int i = 0; i != DATASIZE; ++i){
		int _; scanf("%d", &_);
		for (int j = 0; j != FEATSIZE; ++j)
			scanf("%lf", &X[i][j]);
		scanf("%d", &Y[i]);
		parent.label[Y[i]]++;
	}

	// printf("%d %d\n", parent.label[0], parent.label[1]);
	zeros = parent.label[0], ones = parent.label[1];

	IdxFt indices[DATASIZE] = {};
	for (int i = 0; i != DATASIZE; ++i)
		indices[i].idx = i;

	Node* trees = build(indices, DATASIZE, parent);
	return 0;
}