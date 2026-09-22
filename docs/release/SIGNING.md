# Signing commits and release tags

Release maintainers must create annotated, signed `v<version>` tags that point directly to the
reviewed release commit. Both release workflows reject unsigned tags, lightweight tags, signatures
GitHub cannot verify, and tags that do not match the release version or source commit.

Contributors do not need permission to create release tags to submit a pull request. These docs
do not impose a blanket signed-commit requirement on contributions; follow the target branch's
active protection rules. Signing a commit identifies its signer but does not authorize a release.

Use this guide for signing setup, then follow the [release checklist](../../RELEASE_CHECKLIST.md)
and [release automation guide](ORO_RELEASE_AUTOMATION.md). Pushing a matching release tag starts
publication; an environment approval is required only when a reviewer rule is configured.

## Choose a signing method

SSH is the primary example below. An existing GPG setup is also supported; see
[GPG signing](#gpg-signing). Configure one method for this checkout.

Commands use Bash from the repository root, including Git Bash on Windows. Use Git 2.34 or newer
for SSH signing and a current OpenSSH installation with `ssh-keygen -Y` support. Check your tools:

```sh
git --version
ssh -V
```

The examples use `git config --local` to affect only this checkout. Use `--global` instead only
if you want the same settings across repositories. Repository-local settings override global ones.
See [GitHub's signing configuration reference](https://docs.github.com/en/authentication/managing-commit-signature-verification/telling-git-about-your-signing-key).

## Set up SSH signing

### 1. Select a key and identity

Use an existing Ed25519 key or generate a dedicated signing key at an unused path:

```sh
mkdir -p "$HOME/.ssh"
ssh-keygen -t ed25519 -C "YOUR_VERIFIED_GITHUB_EMAIL" -f "$HOME/.ssh/oro_signing"
```

Replace the email placeholder with an email verified on your GitHub account. If the key file
already exists, use it or choose another filename; do not overwrite it. Keep the private key
local. The `.pub` file is the public key to register and use in the examples below.

Set these variables to your actual values, reusing an existing key path if appropriate:

```sh
oro_signing_email='YOUR_VERIFIED_GITHUB_EMAIL'
oro_signing_key="$HOME/.ssh/oro_signing.pub"
ssh-keygen -lf "$oro_signing_key"
```

If your key needs an agent, load its private counterpart into your existing SSH agent:

```sh
ssh-add "${oro_signing_key%.pub}"
```

If no agent is running, follow the agent setup for your operating system. Hardware-backed keys
may require a touch or PIN when signing. OpenSSH documents key generation, agents, and detached
signatures in the [ssh-keygen manual](https://man.openbsd.org/ssh-keygen).

### 2. Register the public key on GitHub

Open your GitHub account's **Settings → SSH and GPG keys → New SSH key**. Select **Signing Key**,
give it a recognizable title, and paste the contents of the public key file:

```sh
cat "$oro_signing_key"
```

A key registered only for authentication does not establish signing trust. You can register the
same public key separately for authentication and signing. Never upload the private key.
See [GitHub's key-registration instructions](https://docs.github.com/en/authentication/connecting-to-github-with-ssh/adding-a-new-ssh-key-to-your-github-account).

If you use GitHub CLI, inspect the account's registered signing keys with:

```sh
gh api user/ssh_signing_keys --jq '.[] | {id, title, key}'
```

Compare the key type and base64 key material with your `.pub` file; comments can differ.

### 3. Configure Git

```sh
git config --local user.email "$oro_signing_email"
git config --local gpg.format ssh
git config --local user.signingkey "$oro_signing_key"
git config --local tag.gpgSign true
```

Optionally enable signing for every new contributor commit in this checkout:

```sh
git config --local commit.gpgSign true
```

Without that optional setting, use `git commit -S` when making a commit you want to sign.
These settings do not change existing commits or tags.

### 4. Configure local signature verification

GitHub registration and local verification use separate trust stores. Git needs an allowed-signers
file to trust SSH signatures when running `git verify-commit` or `git tag -v`.

First inspect any existing configuration:

```sh
git config --show-origin --get gpg.ssh.allowedSignersFile
```

If it is already configured, set `oro_allowed_signers` below to that file's absolute path.
Otherwise use the default shown here. Append your trusted public key without replacing other
entries, then configure this checkout:

```sh
oro_allowed_signers="$HOME/.config/git/allowed_signers"
mkdir -p "$(dirname "$oro_allowed_signers")"
oro_signer_entry="$(printf '%s %s' "$oro_signing_email" "$(cat "$oro_signing_key")")"
if ! grep -Fqx -- "$oro_signer_entry" "$oro_allowed_signers" 2>/dev/null; then
  printf '%s\n' "$oro_signer_entry" >> "$oro_allowed_signers"
fi
git config --local gpg.ssh.allowedSignersFile "$oro_allowed_signers"
```

This file belongs outside the repository. To verify another maintainer locally, independently
confirm their key fingerprint and add their public key with their identity; do not trust a key
merely because it appears in an unreviewed contribution. See
[Git's allowed-signers configuration](https://git-scm.com/docs/git-config#Documentation/git-config.txt-gpgsshallowedSignersFile).

### 5. Check signing without creating a tag

The following signs a temporary message and verifies it against your configured trust file.
It does not create Git objects, change refs, or start a release. Run it after configuring SSH
signing above; the example assumes `user.signingkey` is a public-key file path.

```sh
oro_signing_check_dir="$(mktemp -d)"
oro_signing_key="$(git config --path --get user.signingkey)"
oro_signing_email="$(git config --get user.email)"
oro_allowed_signers="$(git config --path --get gpg.ssh.allowedSignersFile)"
printf '%s\n' 'Oro Runtime signing configuration check; this is not a release.' \
  > "$oro_signing_check_dir/message"

ssh-keygen -Y sign -f "$oro_signing_key" -n git "$oro_signing_check_dir/message"
ssh-keygen -Y verify -f "$oro_allowed_signers" -I "$oro_signing_email" \
  -n git -s "$oro_signing_check_dir/message.sig" < "$oro_signing_check_dir/message"
```

Both commands must exit successfully; verification should report a good signature for the
expected identity and fingerprint. This proves local key access and trust, not GitHub's eventual
verification of a release tag. The temporary message and signature remain available for inspection.

## GPG signing

If you already use GPG, list your signing keys and select the intended full fingerprint:

```sh
gpg --list-secret-keys --keyid-format=long
```

If necessary, [generate a GPG key](https://docs.github.com/en/authentication/managing-commit-signature-verification/generating-a-new-gpg-key)
with an email verified on GitHub. Replace the placeholders below, export only the public key,
and add it under **GitHub Settings → SSH and GPG keys → New GPG key**:

```sh
oro_gpg_fingerprint='YOUR_FULL_GPG_FINGERPRINT'
git config --local user.email 'YOUR_VERIFIED_GITHUB_EMAIL'
git config --local gpg.format openpgp
git config --local user.signingkey "$oro_gpg_fingerprint"
git config --local tag.gpgSign true
gpg --armor --export "$oro_gpg_fingerprint"
```

For a check without creating a tag:

```sh
oro_signing_check_dir="$(mktemp -d)"
printf '%s\n' 'Oro Runtime signing configuration check; this is not a release.' \
  > "$oro_signing_check_dir/message"
gpg --local-user "$oro_gpg_fingerprint" --armor --detach-sign \
  "$oro_signing_check_dir/message"
gpg --verify "$oro_signing_check_dir/message.asc" "$oro_signing_check_dir/message"
```

GPG uses its own keyring and trust configuration; the SSH allowed-signers file is not involved.
Optional commit signing still uses `git config --local commit.gpgSign true` or `git commit -S`.

## Create a release tag only after the release gates pass

Complete the [release checklist](../../RELEASE_CHECKLIST.md), including account activation,
the full CI matrix on the exact final commit, and review of the release artifacts. A green
documentation-only run with native jobs skipped does not satisfy the full CI gate.

Run the nonpublishing artifact preflight on the release branch, using `0.1.0` as an example:

```sh
gh workflow run release-artifacts.yml --repo oro-computer/runtime --ref master \
  -f version=0.1.0 -f artifact_id=all
```

Confirm its source SHA matches the final CI-tested commit and all eight distributions pass.
Branch preflights do not generate tag attestations or run the npm packaging workflow. The tagged
run performs those steps before publication. Do not dispatch this preflight on a release tag:
the workflow's publication conditions use the tag ref, including for manually dispatched runs.

From the clean, reviewed checkout, check the version and capture the final commit:

```sh
git status --short
node bin/check-release-version.js 0.1.0
oro_release_sha="$(git rev-parse HEAD)"
printf '%s\n' "$oro_release_sha"
```

Require an empty status and compare that SHA with the successful full CI and preflight runs.
Only a release maintainer should then create the tag:

```sh
git tag -s v0.1.0 "$oro_release_sha" -m "Oro Runtime 0.1.0"
git tag -v v0.1.0
git rev-parse 'v0.1.0^{commit}'
```

Confirm the signer, fingerprint, and target commit. Creating the local tag does not publish.
When ready to start the release, push only that tag:

```sh
git push origin refs/tags/v0.1.0
```

Confirm GitHub reports the signature as **Verified**. With GitHub CLI, inspect the same tag object
the release workflow validates:

```sh
oro_tag_object="$(gh api repos/oro-computer/runtime/git/ref/tags/v0.1.0 --jq .object.sha)"
gh api "repos/oro-computer/runtime/git/tags/$oro_tag_object" \
  --jq '{verification: .verification | {verified, reason}, target: .object}'
```

Require `verified: true` and a target of type `commit` with the expected SHA. Follow the release
run through artifact checks, npm installation checks, the configured `npm-publish` approval,
npm publication, and GitHub release creation. Local signature verification alone does not satisfy
the workflow's GitHub verification gate.

Do not move or overwrite a published release tag. Source corrections require a new version.

## Troubleshooting

| Symptom                                                                    | Check                                                                                                                                                                                                                   |
| -------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `gpg.ssh.allowedSignersFile` is missing or verification finds no principal | Configure the file and confirm it contains the signer's trusted public key and identity.                                                                                                                                |
| SSH signing cannot load a key or the agent refuses to sign                 | Check the configured key path, private-key access, agent, passphrase, and any hardware-key prompt.                                                                                                                      |
| Git invokes GPG when you intended SSH                                      | Inspect `git config --show-origin --get gpg.format` and `git config --show-origin --get user.signingkey`; local settings can override global ones.                                                                      |
| Local verification passes but GitHub reports an unverified signature       | Confirm the key is registered to the intended account as a signing key, the identity is correct, and inspect the API's verification reason. GPG also requires the appropriate verified email and registered public key. |
| The workflow rejects an annotated or version-mismatched tag                | An annotated tag must also be signed. Check the `v<version>` spelling, synchronized manifests, direct commit target, and GitHub verification result.                                                                    |
| The release waits for approval                                             | An authorized reviewer must approve the `npm-publish` deployment after inspecting the artifacts. This is separate from signature verification.                                                                          |
| npm authentication fails after signature verification                      | Check the package's trusted publisher and environment configuration in the release automation guide. Your personal signing key does not authenticate npm publication.                                                   |

For additional background, see [GitHub's signed-tag instructions](https://docs.github.com/en/authentication/managing-commit-signature-verification/signing-tags).
