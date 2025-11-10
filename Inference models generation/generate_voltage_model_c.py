"""
Generate C code for Voltage RandomForest Model
Uses m2cgen to convert pruned voltage model to C
"""

import joblib
import m2cgen
import os

# Configuration
MODELS_DIR = 'deploy/models'
C_CODE_DIR = 'deploy/c_code'
PANEL = '+X'

# Find the most recent voltage model (pruned version for smaller size)
voltage_models = [f for f in os.listdir(MODELS_DIR) if f.startswith('voltage_rf_pruned50') and PANEL in f]
if not voltage_models:
    print(f"ERROR: No voltage models found for panel {PANEL}")
    exit(1)

# Use the most recent one (highest timestamp)
voltage_model_file = sorted(voltage_models)[-1]
voltage_model_path = os.path.join(MODELS_DIR, voltage_model_file)

print(f"Loading voltage model: {voltage_model_path}")

# Load the model (using joblib, not pickle)
voltage_model = joblib.load(voltage_model_path)

print(f"Model loaded: {type(voltage_model)}")
print(f"Model features: {voltage_model.n_features_in_}")

# Generate C code
print("Generating C code...")
c_code = m2cgen.export_to_c(voltage_model, function_name='score_voltage')

# Save to file
output_path = os.path.join(C_CODE_DIR, 'voltage_model.c')
with open(output_path, 'w') as f:
    f.write(c_code)

print(f"✓ C code generated: {output_path}")
print(f"✓ Function name: score_voltage(double* input)")
print(f"✓ Input size: {voltage_model.n_features_in_} features")

# Verify file size
file_size = os.path.getsize(output_path)
print(f"✓ File size: {file_size / 1024:.1f} KB")

# Show first few lines
print("\nFirst 20 lines of generated code:")
with open(output_path, 'r') as f:
    lines = f.readlines()
    for i, line in enumerate(lines[:20]):
        print(f"{i+1:3d}: {line}", end='')

print("\n" + "="*60)
print("SUCCESS! Voltage model C code generated.")
print("="*60)
print("\nNext steps:")
print("1. Update eps_main_deployment.c line 203:")
print("   Replace: return (float)features[0];")
print("   With:    return (float)score_voltage(features);")
print("2. Add header: #include \"voltage_model.h\"")
print("3. Recompile and test")
