/* Set eq stuff. Struct, defines. */


struct set_data
{
  char *SetName;
  int vnum[19];
  char *Set_Wear_Message;
  char *Set_Remove_Message;
};

#define BASE_SETS                     1200
#define SET_MAX 		       1204
// Eq_sets.

#define SET_SAIYAN 0
#define SET_VESTMENTS 1
#define SET_HUNTERS 2
#define SET_CAPTAINS 3
#define SET_CELEBRANTS 4
#define SET_RAGER 5
#define SET_FIELDPLATE 6
#define SET_MOAD 7

extern const struct set_data set_list[];
 
