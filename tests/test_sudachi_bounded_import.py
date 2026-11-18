import csv, hashlib, importlib.util, json, pathlib, subprocess, sys, tempfile, unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]
IMPORTER = ROOT / "tools" / "import_sudachi_v1.py"
def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
validate = load("validate_sudachi", ROOT / "tools" / "validate_sudachi_pack_inputs.py")
importer = load("import_sudachi", IMPORTER)


class BoundedImportTests(unittest.TestCase):
    def test_validator_detects_duplicate_zero_cost_cell_on_disk(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            lex = root / "lex.csv"
            matrix = root / "matrix.def"
            lex.write_text("SURFACE,LEFT_ID,RIGHT_ID\n語,1,2\n", encoding="utf-8")
            matrix.write_text("3 4\n0 0 0\n0 0 0\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate"):
                validate.validate(lex, matrix)

    def test_validator_reports_valid_large_stream_without_retaining_rows(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            lex = root / "lex.csv"
            matrix = root / "matrix.def"
            with lex.open("w", encoding="utf-8") as stream:
                stream.write("SURFACE,LEFT_ID,RIGHT_ID\n")
                for i in range(5000):
                    stream.write(f"語{i},1,2\n")
            with matrix.open("w", encoding="utf-8") as stream:
                stream.write("5000 5000\n")
                for i in range(5000):
                    stream.write(f"{i} {i} {i - 2500}\n")
            result = validate.validate(lex, matrix)
            self.assertEqual(result["lexicon_rows"], 5000)
            self.assertEqual(result["matrix_rows"], 5000)
            self.assertIn("SQLite", result["validation_storage"])

    def test_validator_allows_minus_one_composition_only_context_ids(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            lex, matrix = root / "lex.csv", root / "matrix.def"
            lex.write_text("SURFACE,LEFT_ID,RIGHT_ID\n語,1,2\n構成,-1,2\n", encoding="utf-8")
            matrix.write_text("3 4\n0 0 0\n", encoding="utf-8")
            result = validate.validate(lex, matrix)
            self.assertEqual(result["lexicon_rows"], 2)
            self.assertEqual(result["non_decodable_rows"], 1)

    def test_importer_skips_composition_only_entries_with_minus_one_id(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            source, output = root / "lex.csv", root / "lex.tsv"
            header = ["Index_Form","Left_Id","Right_Id","Cost","Headword","POS1","POS2","POS3","POS4","POS5","POS6","Reading_Form","Normalized_Form","Dictionary_Form","Mode","Split_A","Split_B","Word_Structure","Synonym_Groups","Split_C","User_Data","POS_Id","Reference_Id"]
            rows = [
                ["あめ","1","2","-5","雨","名詞","普通名詞","一般","*","*","*","アメ","雨","雨","A","","","","","","","",""],
                ["構成","-1","2","0","構成","名詞","普通名詞","一般","*","*","*","コウセイ","","","A","","","","","","","",""],
            ]
            with source.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.writer(stream); writer.writerow(header); writer.writerows(rows)
            result = importer.import_lexicon(source, output)
            self.assertEqual(result["entries"], 1)
            self.assertEqual(result["non_decodable_skipped"], 1)
            self.assertEqual(len(output.read_text(encoding="utf-8").splitlines()), 2)

    def test_matrix_import_is_sorted_deduplicated_and_keeps_zero_only_for_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            source, output = root / "matrix.def", root / "connections.tsv"
            source.write_text("3 4\n2 1 -20\n0 0 0\n1 2 5\n", encoding="utf-8")
            result = importer.import_matrix(source, output)
            self.assertEqual(result["input_cells"], 3)
            self.assertEqual(result["nonzero_cells"], 2)
            self.assertEqual(output.read_text(encoding="utf-8").splitlines()[2:], ["1\t2\t5", "2\t1\t-20"])
            source.write_text("3 4\n0 0 0\n0 0 1\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate"):
                importer.import_matrix(source, output)

    def test_lexicon_import_is_deterministic_and_deduplicates_on_disk(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            source, output = root / "lex.csv", root / "lex.tsv"
            header = ["SURFACE","LEFT_ID","RIGHT_ID","COST","WRITING","POS1","POS2","POS3","POS4","POS5","POS6","READING_FORM","NORMALIZED_FORM","DICTIONARY_FORM","MODE","SPLIT_A","SPLIT_B","WORD_STRUCTURE"]
            row = ["雨","1","2","-5","雨","名詞","普通名詞","一般","*","*","*","アメ","雨","雨","A","","",""]
            with source.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.writer(stream); writer.writerow(header); writer.writerow(row); writer.writerow(row)
            result = importer.import_lexicon(source, output)
            self.assertEqual(result["entries"], 1)
            self.assertIn("SQLite", result["import_storage"])
            self.assertEqual(len(output.read_text(encoding="utf-8").splitlines()), 2)


if __name__ == "__main__":
    unittest.main()
