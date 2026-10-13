$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$true
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Profile compatibility testing is allowed only in GitHub Actions.' }
$Check=@'
import json, pathlib, sys
root=pathlib.Path.cwd()
sys.path.insert(0,str(root/'ci-output/oracle'))
from config_manager import GhostWaveConfig
actual=GhostWaveConfig.from_encrypted_bytes((root/'ci-output/native/native-profile.sn').read_bytes()).to_dict()
expected=json.loads((root/'ci-output/reference/legacy-profile.json').read_text(encoding='utf-8'))
if actual != expected:
    raise RuntimeError('The historical application could not preserve the native profile fields')
(root/'ci-output/profile-results.json').write_text(json.dumps(dict(passed=True,directions=['Python to C++','C++ to Python'])),encoding='utf-8')
print('Profile compatibility passed in both directions')
'@
$Check | Set-Content ci-output/check-profile.py -Encoding utf8
python ci-output/check-profile.py
