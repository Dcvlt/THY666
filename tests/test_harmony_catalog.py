"""Synthetic merge invariants: source musical layouts are never rewritten."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from merge_harmony_catalog import merge, header, BASE_SCALES, steps_mask

class CatalogTests(unittest.TestCase):
    def test_union_keeps_ids_slots_and_aliases(self):
        records=[{'name':'Cmaj','notes':[{'midi':60},{'midi':64},{'midi':67}],
                  'midi_slots':[60,64,67,0,0,0,0]},
                 {'name':'C','notes':[{'midi':60},{'midi':62},{'midi':64},{'midi':65},{'midi':67},{'midi':69},{'midi':71}]},
                 {'name':'Cno1','notes':[{'midi':64},{'midi':67}]}]
        pads=[[1,0],[2,2]]+[None]*46+[[3,1]]+[None]*47
        projection={'groups':[{'index':0,'name':'Ionian'}],
                    'scales':[{'group_index':0,'root_label':'C','pads':pads}],
                    'pad_records':records}
        before=copy.deepcopy(projection)
        result=merge(projection,[('Major alias',0xab5),('New scale',0x097)],
                     [(['major'],0x091),(['New chord'],0x097)])
        self.assertEqual(projection,before)
        self.assertEqual(result['original_projection'],before)
        self.assertEqual([(s['name'],s['mask']) for s in result['scales'][:16]],BASE_SCALES)
        self.assertEqual(result['scales'][0]['aliases'],['Ionian','Major alias'])
        self.assertEqual(result['chord_masks'].count(0x091),1)
        self.assertEqual(result['rootless_record_indices'],[2])
        self.assertIn(0x097,result['chord_masks'])
        self.assertEqual(result,merge(projection,[('Major alias',0xab5),('New scale',0x097)],
                                      [(['major'],0x091),(['New chord'],0x097)]))

    def test_header_keeps_all_playable_family_ids(self):
        # The generated device table has 70 slots. Exercise that contract with
        # synthetic families rather than weakening it for the small merge test.
        records=[{'name':'C','notes':[{'midi':60},{'midi':64},{'midi':67}]},
                 {'name':'C','notes':[{'midi':60},{'midi':62},{'midi':64},{'midi':65},
                                      {'midi':67},{'midi':69},{'midi':71}]},
                 {'name':'C','notes':[{'midi':60},{'midi':62},{'midi':63},{'midi':65},
                                      {'midi':67},{'midi':69},{'midi':70}]}]
        projection={'groups':[{'index':i,'name':'Synthetic '+str(i)} for i in range(70)],
                    'scales':[{'group_index':i,'root_label':'C',
                               'pads':[[0,0]]+[None]*47+[[0,1+i%2]]+[None]*47}
                              for i in range(70)],
                    'pad_records':records}
        before=copy.deepcopy(projection)
        result=merge(projection,[],[])
        generated=header(result)
        family_rows=generated.split('guidance_catalog_families[]={\n',1)[1].split('};',1)[0]
        self.assertEqual([int(n.strip()[:-1]) for n in family_rows.strip().split(',')],
                         [i%2 for i in range(70)])
        self.assertEqual(generated,header(merge(projection,[],[])))
        self.assertEqual(projection,before)
        self.assertEqual(result['original_projection'],before)
        projection['groups'].pop()
        projection['scales'].pop()
        with self.assertRaisesRegex(ValueError,'frozen 70-family'):
            header(merge(projection,[],[]))

    def test_invalid_intervals_rejected(self):
        for steps in ([],[0,12],[0,-1],[0,0],[1,2],[True,2]):
            with self.assertRaises(ValueError): steps_mask(steps)
        self.assertEqual(steps_mask([0,4,7]),0x091)

if __name__=='__main__': unittest.main()
