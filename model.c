#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define nitems(_a)     (sizeof((_a)) / sizeof((_a)[0]))
#define cube(_a)     ((_a) * (_a) * (_a))

struct state {
	uid_t ruid;
	uid_t euid;
	uid_t suid;
};

struct change {
	char *name;
	union {
		int (*f1) (uid_t);
		int (*f2) (uid_t, uid_t);
		int (*f3) (uid_t, uid_t, uid_t);
	} func;
	unsigned int nargs;
	int arg1;
	int arg2;
	int arg3;
};

struct graph {
	struct state *states;
	unsigned int nstates;
	struct change *changes;
	unsigned int nchanges;
};

/*
 * 1. Call getresuid(&r,&e,&s).
 * 2. Return (r, e, s).
 */
int
getmystate(struct state *s)
{
	return getresuid(&(s->ruid), &(s->euid), &(s->suid));
}

/*
 * 1. Call setresuid(r, e, s).
 * 2. Check for error.
 */
void
setmystate(struct state *s)
{
	setresuid(s->ruid, s->euid, s->suid);
}

/*
 * 1. Pick n arbitrary uids u1, . . . , un.
 * 2. Let U := {u1, . . . , un}.
 * 3. Let S := {(r, e, s) : r, e, s ∈ U }.
 * 4. Let C := {setuid(x), setreuid(x, y),
 *              setresuid(x, y, z), ...
 *               : x, y, z in (U union {-1})}.
 * 5. Return (S, C).
*/
void
getallmystates(struct graph *g)
{
	int i, j, k, n = 0;
	int U[] = {0, 1000}; //{0, 1, 2, 1000, 32767};
	int U2[] = {0, 1000 }; //{0, 1, 2, 1000, 32767, -1};
	struct state *S;
	struct change *C;
	if ((S = malloc(cube(nitems(U)) * sizeof(struct state))) == NULL)
		err(1, "malloc");
	if ((C = malloc(4 * cube(nitems(U2)) * sizeof(struct change))) == NULL)
		err(1, "malloc");

	for (i = 0; i < nitems(U); i++)
		for (j = 0; j < nitems(U); j++)
			for (k = 0; k < nitems(U); k++) {
				S[n].ruid = U[i];
				S[n].euid = U[j];
				S[n].suid = U[k];
				n++;
			}
	g->states = S;
	g->nstates = n;

	n = 0;
	for (i = 0; i < nitems(U2); i++) {
		C[n].name = "setuid";
		C[n].func.f1 = setuid;
		C[n].nargs = 1;
		C[n].arg1 = U2[i];
		n++;
		C[n].name = "seteuid";
		C[n].func.f1 = seteuid;
		C[n].nargs = 1;
		C[n].arg1 = U2[i];
		n++;
		for (j = 0; j < nitems(U2); j++) {
			C[n].name = "setreuid";
			C[n].func.f2 = setreuid;
			C[n].nargs = 2;
			C[n].arg1 = U2[i];
			C[n].arg2 = U2[j];
			n++;
			for (k = 0; k < nitems(U2); k++) {
				C[n].name = "setresuid";
				C[n].func.f3 = setresuid;
				C[n].nargs = 3;
				C[n].arg1 = U2[i];
				C[n].arg2 = U2[j];
				C[n].arg3 = U2[k];
				n++;
			}
		}
	}

	g->changes = C;
	g->nchanges = n;
}

void
invoke(struct change *c)
{
	switch (c->nargs) {
	case 1:
		c->func.f1(c->arg1);
		break;
	case 2:
		c->func.f2(c->arg1, c->arg2);
		break;
	default:
		c->func.f3(c->arg1, c->arg2, c->arg3);
		break;
	}
}

void
printlabel(struct change *c)
{
	printf("%s(", c->name);
	switch (c->nargs) {
	case 1:
		printf("%d", c->arg1);
		break;
	case 2:
		printf("%d,%d", c->arg1, c->arg2);
		break;
	default:
		printf("%d,%d,%d", c->arg1, c->arg2, c->arg3);
		break;
	}
	printf(")");
}

void
printstate(struct state *s)
{
	printf("\"R=%d,E=%d,S=%d\"", s->ruid, s->euid, s->suid);
}

/*
 * 1. Let (S, C) := GETALLSTATES().
 * 2. Create an empty FSA with statespace S.
 * 3. For each s in S, do:
 * 4.  For each c in C, do:
 * 5.   Fork a child process, and within the child, do:
 * 6.    Call SETSTATE(s), and then invoke c.
 * 7.    Finally, let s' := GETSTATE(), pass s' to the parent process, and exit.
 * 8.   Add the transition s -c-> s' to the FSA.
 * 9. Return the newly-constructed FSA as the model.
 */
void
buildmymodel(void)
{
	int s, c, status;
	pid_t pid;
	struct state s2;
	struct graph g;
	getallmystates(&g);

	puts("digraph G {");

	
	for (s = 0; s < g.nstates; s++) {
	printf("#%d / %d\n", s, g.nstates);
		printstate(&g.states[s]);
		puts("");
		fflush(stdout);
		for (c = 0; c < g.nchanges; c++) {
again:
			switch (pid = fork()) {
			case -1:
				printf("# fork!\n");
				goto again;
				//err(1, "fork");
			case 0:
				setmystate(&g.states[s]);
				invoke(&g.changes[c]);
				getmystate(&s2);
				printstate(&g.states[s]);
				printf("->");
				printstate(&s2);
				printf("[label=\"");
				printlabel(&g.changes[c]);
				printf("\"]\n");
				exit(0);
				break;
			default:
				while (waitpid(pid, &status, 0) == -1 && errno == EINTR)
					//if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
					/* nothing */
				printf("# wait done\n");
				break;
			}
		}
	}
	puts("}");
}

int
main(void)
{
	
	/buildmymodel();
	return 0;
}
