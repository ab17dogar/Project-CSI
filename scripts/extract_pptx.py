import zipfile
import re
import sys

def extract_text_from_pptx(pptx_path):
    try:
        with zipfile.ZipFile(pptx_path, 'r') as zip_ref:
            # Sort slides by their number in the filename
            slide_files = [f for f in zip_ref.namelist() if f.startswith('ppt/slides/slide') and f.endswith('.xml')]
            slide_files.sort(key=lambda x: int(re.search(r'slide(\d+)', x).group(1)))
            
            for slide_file in slide_files:
                print(f"--- {slide_file.upper().replace('PPT/SLIDES/', '').replace('.XML', '')} ---")
                content = zip_ref.read(slide_file).decode('utf-8')
                # Find all text within <a:t> tags
                text_matches = re.findall(r'<a:t>(.*?)</a:t>', content)
                print(" ".join(text_matches))
                print()
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        extract_text_from_pptx(sys.argv[1])
    else:
        print("Usage: python3 extract_pptx.py <path_to_pptx>")
